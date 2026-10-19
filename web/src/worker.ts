// The engine runs here, off the page's thread.
//
// in:  {type: 'init', engine, module, snapshot, names, files}  engine is the
//      URL of hdTransEngine.js, module the compiled hdTransEngine.wasm,
//      snapshot the ArrayBuffer of the compiled column dictionaries, files[i]
//      that of the other dictionary file names[i]
//      {type: 'translate', id, mode, lines, userdict}
// out: {type: 'ready', ms, memory} | {type: 'fatal', message}
//      {type: 'result', id, lines, ms, warning, memory} | {type: 'error', id, message}
// memory is the size of the engine's WebAssembly memory in bytes. A user
// dictionary that does not load is left out; the translation goes ahead and
// warning holds the error.

interface Module {
  FS: { mkdir(path: string): void; writeFile(path: string, data: Uint8Array | string): void; unlink(path: string): void };
  HEAPU8: Uint8Array;
  ccall(name: string, ret: string, types: string[], args: unknown[]): number;
  getValue(ptr: number, type: string): number;
  UTF8ToString(ptr: number, n?: number): string;
  lengthBytesUTF8(s: string): number;
  stringToUTF8(s: string, ptr: number, n: number): void;
  _malloc(n: number): number;
  _free(p: number): void;
  _hdTrans_Terminate(): void;
  _hdTrans_Reset(): number;
  _hdTrans_FreeMem(p: number): void;
  _hdTrans_Translate(text: number, n: number, out: number, outn: number): number;
  _hdTrans_UserDictError(out: number, outn: number): number;
}

let m: Module | undefined;
let loaded = ''; // the user dictionary text the engine has
let warning = '';

/** Calls f with the two out parameters of a buffer-returning function and returns the buffer's text. */
function buffer(e: Module, f: (out: number, outn: number) => number): string {
  const p = e._malloc(8);
  try {
    if (f(p, p + 4) !== 0) throw new Error('hdtrans: the engine is not loaded');
    const s = e.getValue(p, '*');
    const text = e.UTF8ToString(s, e.getValue(p + 4, 'i32'));
    e._hdTrans_FreeMem(s);
    return text;
  } finally {
    e._free(p);
  }
}

function translate(e: Module, text: string): string {
  const n = e.lengthBytesUTF8(text);
  const p = e._malloc(n + 1);
  try {
    e.stringToUTF8(text, p, n + 1);
    return buffer(e, (out, outn) => e._hdTrans_Translate(p, n, out, outn));
  } finally {
    e._free(p);
  }
}

/** Loads the compiled /dicts.hdsnap, the other files of /dicts and /userdict.tsv. */
function initialize(e: Module) {
  e._hdTrans_Terminate();
  if (e.ccall('hdTrans_Initialize', 'number', ['string'], ['/dicts.hdsnap\0/dicts\0']) !== 0) {
    throw new Error('hdtrans: the dictionaries do not load');
  }
  warning = buffer(e, (out, outn) => e._hdTrans_UserDictError(out, outn)).replace(/^\/userdict\.tsv:/gm, '');
}

self.onmessage = async (ev: MessageEvent) => {
  const msg = ev.data;
  if (msg.type === 'init') {
    try {
      const t0 = performance.now();
      const { default: create } = await import(msg.engine);
      const e: Module = await create({
        instantiateWasm(imports: WebAssembly.Imports, done: (i: WebAssembly.Instance) => void) {
          WebAssembly.instantiate(msg.module as WebAssembly.Module, imports).then(done);
          return {};
        },
      });
      e.FS.writeFile('/dicts.hdsnap', new Uint8Array(msg.snapshot));
      e.FS.mkdir('/dicts');
      msg.names.forEach((n: string, i: number) => e.FS.writeFile('/dicts/' + n, new Uint8Array(msg.files[i])));
      initialize(e);
      m = e;
      postMessage({ type: 'ready', ms: performance.now() - t0, memory: e.HEAPU8.length });
    } catch (err) {
      postMessage({ type: 'fatal', message: String(err) });
    }
    return;
  }
  if (msg.type !== 'translate' || !m) return;
  const e = m;
  try {
    const t0 = performance.now();
    if (msg.userdict !== loaded) {
      loaded = msg.userdict;
      if (msg.userdict) e.FS.writeFile('/userdict.tsv', msg.userdict);
      else e.FS.unlink('/userdict.tsv');
      initialize(e);
    }
    let lines: string[];
    if (msg.mode === 'document') {
      e._hdTrans_Reset();
      lines = translate(e, msg.lines.join('\n')).split('\n');
    } else {
      lines = msg.lines.map((l: string) => {
        e._hdTrans_Reset();
        return translate(e, l);
      });
    }
    postMessage({ type: 'result', id: msg.id, lines, ms: performance.now() - t0, warning, memory: e.HEAPU8.length });
  } catch (err) {
    postMessage({ type: 'error', id: msg.id, message: String(err) });
  }
};
