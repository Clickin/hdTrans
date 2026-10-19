import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

// With SNAPSHOT, a snapshot compiled from the folder of DICTIONARY.tsv alone (hdtrans -compile-snapshot), the
// engine also loads it in place of the column file and must translate the same way.
const [modulePath, fixturePath, snapshotPath] = process.argv.slice(2);
assert(modulePath && fixturePath, 'usage: wasm_smoke.mjs MODULE.js DICTIONARY.tsv [SNAPSHOT.hdsnap]');
const { default: createEngine } = await import(pathToFileURL(resolve(modulePath)));
const engine = await createEngine({
    wasmBinary: await readFile(join(dirname(resolve(modulePath)), 'hdTransEngine.wasm')),
});
engine.FS.mkdir('/dicts');
engine.FS.writeFile('/dicts/base.tsv', await readFile(fixturePath));
assert.equal(engine.ccall('hdTrans_Initialize', 'number', ['string'], ['/dicts\0']), 0);

const text = '学生';
function translate() {
    const output = engine._malloc(8);
    let translated = 0;
    try {
        assert.equal(engine.ccall('hdTrans_Translate', 'number', ['string', 'number', 'number', 'number'],
            [text, Buffer.byteLength(text), output, output + 4]), 0);
        translated = engine.getValue(output, '*');
        const length = engine.getValue(output + 4, 'i32');
        assert(translated && length > 0, 'translation must be non-empty');
        const result = engine.UTF8ToString(translated, length);
        assert(result.length > 0);
        assert.equal(engine._hdTrans_Reset(), 0);
        return result;
    } finally {
        if (translated) engine._hdTrans_FreeMem(translated);
        engine._free(output);
    }
}
try {
    const result = translate();
    console.log(`PASS wasm smoke: ${text} -> ${result}`);
    if (snapshotPath) {
        engine._hdTrans_Terminate();
        engine.FS.writeFile('/dicts.hdsnap', await readFile(snapshotPath));
        assert.equal(engine.ccall('hdTrans_Initialize', 'number', ['string'], ['/dicts.hdsnap\0/dicts\0']), 0);
        assert.equal(translate(), result);
        console.log(`PASS wasm smoke: compiled snapshot, ${text} -> ${result}`);
    }
} finally {
    engine._hdTrans_Terminate();
}
