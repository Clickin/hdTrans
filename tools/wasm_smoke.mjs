import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const [modulePath, fixturePath] = process.argv.slice(2);
assert(modulePath && fixturePath, 'usage: wasm_smoke.mjs MODULE.js DICTIONARY.tsv');
const { default: createEngine } = await import(pathToFileURL(resolve(modulePath)));
const engine = await createEngine({
    wasmBinary: await readFile(join(dirname(resolve(modulePath)), 'hdTransEngine.wasm')),
});
engine.FS.mkdir('/dicts');
engine.FS.writeFile('/dicts/base.tsv', await readFile(fixturePath));
assert.equal(engine.ccall('hdTrans_Initialize', 'number', ['string'], ['/dicts\0']), 0);

const text = '学生';
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
    console.log(`PASS wasm smoke: ${text} -> ${result}`);
} finally {
    if (translated) engine._hdTrans_FreeMem(translated);
    engine._free(output);
    engine._hdTrans_Terminate();
}
