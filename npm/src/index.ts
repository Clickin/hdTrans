import { readFile } from 'node:fs/promises';
import createEngine, { type EngineModule } from './hdTransEngine.js';
import { HdTrans as RuntimeHdTrans, type InitOptions as RuntimeInitOptions } from './runtime.js';

export type {
  UserDictPos,
  UserDictEntry,
  ChoiceSelection,
  TranslationChoice,
  TranslationResult,
  EhndFiles,
} from './runtime.js';

export type InitOptions = Pick<RuntimeInitOptions, 'ehnd'>;
export type HdTrans = RuntimeHdTrans;

let modulePromise: Promise<EngineModule> | undefined;

async function loadModule(): Promise<EngineModule> {
  const [wasmBinary, manifest] = await Promise.all([
    readFile(new URL('./hdTransEngine.wasm', import.meta.url)),
    readFile(new URL('./dictionary.json', import.meta.url), 'utf8').then(JSON.parse),
  ]);
  const module = await createEngine({ wasmBinary });
  module.FS.mkdir('/dicts');
  for (const file of ['dicts.hdsnap', 'dicts.txt', ...manifest.files]) {
    module.FS.writeFile(`/${file}`, await readFile(new URL(file, import.meta.url)));
  }
  return module;
}

/** Uses the bundled engine and dictionary. Each call returns an independent translator. */
export const HdTrans = Object.freeze({
  async init(options: InitOptions = {}): Promise<HdTrans> {
    modulePromise ??= loadModule().catch((error) => {
      modulePromise = undefined;
      throw error;
    });
    return RuntimeHdTrans.init({
      module: await modulePromise,
      directories: ['/dicts.hdsnap', '/dicts'],
      ehnd: options.ehnd,
    });
  },
});
