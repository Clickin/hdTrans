#!/usr/bin/env node
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { chmod, copyFile, mkdir, mkdtemp, readFile, readdir, rename, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { parseArgs } from 'node:util';

const root = fileURLToPath(new URL('../', import.meta.url));
const packageDir = join(root, 'npm');
const { values } = parseArgs({
  options: {
    cli: { type: 'string' },
    wasm: { type: 'string' },
    wrapper: { type: 'string' },
    help: { type: 'boolean' },
  },
});
if (values.help) {
  console.log('Usage: node tools/package-npm.mjs --cli CLI --wasm WASM_DIR [--wrapper TRANSLATOR.js]');
  process.exit(0);
}
const cliArg = values.cli ?? process.env.HDTRANS_CLI;
const wasmArg = values.wasm ?? process.env.HDTRANS_WASM;
if (!cliArg || !wasmArg) throw new Error('provide --cli and --wasm (or HDTRANS_CLI and HDTRANS_WASM) from the same source build');
const cli = resolve(cliArg);
const wasm = resolve(wasmArg);
const wrapper = resolve(values.wrapper ?? process.env.HDTRANS_NPM_WRAPPER ?? join(root, 'build/web/translator.js'));
const dictionary = join(root, 'dicts');
const stage = await mkdtemp(join(packageDir, '.stage-'));
const home = await mkdtemp(join(tmpdir(), 'hdtrans-npm-build-'));

// First nonblank, noncomment line: only a first field exactly "key" is a column header.
function columnFile(text) {
  for (const [i, raw] of text.split('\n').entries()) {
    const line = (i === 0 ? raw.replace(/^\uFEFF/, '') : raw).replace(/\r$/, '');
    if (line !== '' && !line.startsWith('#')) return line.split('\t')[0] === 'key';
  }
  return false;
}

try {
  await mkdir(join(stage, 'dicts'));
  const source = join(stage, 'source');
  await mkdir(source);
  for (const name of ['index.ts', 'cli.ts', 'hdTransEngine.d.ts']) {
    await copyFile(join(packageDir, 'src', name), join(source, name));
  }
  await copyFile(join(dirname(wrapper), 'translator.d.ts'), join(source, 'runtime.d.ts'));
  execFileSync(process.execPath, [
    join(packageDir, 'node_modules/typescript/bin/tsc'),
    '--strict', '--target', 'es2022', '--module', 'nodenext', '--moduleResolution', 'nodenext',
    '--declaration', '--types', 'node', '--typeRoots', join(packageDir, 'node_modules/@types'),
    '--rootDir', source, '--outDir', stage, join(source, 'index.ts'), join(source, 'cli.ts'),
  ], { cwd: packageDir, stdio: 'inherit' });
  await rm(source, { recursive: true });
  await chmod(join(stage, 'cli.js'), 0o755);
  await copyFile(wrapper, join(stage, 'runtime.js'));
  await copyFile(join(dirname(wrapper), 'translator.d.ts'), join(stage, 'runtime.d.ts'));
  for (const name of ['hdTransEngine.js', 'hdTransEngine.wasm']) {
    await copyFile(join(wasm, name), join(stage, name));
  }
  const names = (await readdir(dictionary)).filter((name) => name.endsWith('.tsv')).sort();
  const files = [];
  const sourceHash = createHash('sha256');
  let columns = 0;
  for (const name of names) {
    const content = await readFile(join(dictionary, name));
    sourceHash.update(name).update('\0').update(content).update('\0');
    if (columnFile(content.toString('utf8'))) {
      ++columns;
    } else {
      await copyFile(join(dictionary, name), join(stage, 'dicts', name));
      files.push(`dicts/${name}`);
    }
  }
  if (!columns) throw new Error('repository dicts/ has no column dictionaries');
  const { version } = JSON.parse(await readFile(join(packageDir, 'package.json'), 'utf8'));
  const dictionaryVersion = `${version}+${sourceHash.digest('hex').slice(0, 12)}`;
  await writeFile(join(stage, 'dicts.txt'), `${dictionaryVersion}\n`);
  execFileSync(cli, ['-dicts', dictionary, '-compile-snapshot', join(stage, 'dicts.hdsnap')], {
    cwd: home,
    env: { ...process.env, HOME: home, USERPROFILE: home, HDTRANS_CACHE_DIR: 'off' },
    stdio: ['ignore', 'ignore', 'inherit'],
  });
  const snapshot = await readFile(join(stage, 'dicts.hdsnap'));
  const manifest = {
    version: dictionaryVersion,
    snapshot: {
      file: 'dicts.hdsnap',
      sha256: createHash('sha256').update(snapshot).digest('hex'),
      builtinFingerprint: snapshot.subarray(96, 128).toString('hex'),
      dataFingerprint: snapshot.subarray(128, 160).toString('hex'),
    },
    files,
  };
  await writeFile(join(stage, 'dictionary.json'), `${JSON.stringify(manifest, null, 2)}\n`);

  // Opening checks the native snapshot's builtin/data fingerprints against this WASM.
  const { HdTrans } = await import(pathToFileURL(join(stage, 'index.js')));
  const translator = await HdTrans.init();
  try {
    if (translator.dictionaryVersion !== dictionaryVersion) throw new Error('packaged dictionary version does not match dicts.txt');
    if (!translator.translate('今日は本を読みます。')) throw new Error('empty translation from packaged engine');
  } finally {
    translator.close();
  }
  for (const name of ['LICENSE', 'THIRD-PARTY-NOTICES.txt']) {
    await copyFile(join(root, name), join(packageDir, name));
  }
  await rm(join(packageDir, 'dist'), { recursive: true, force: true });
  await rename(stage, join(packageDir, 'dist'));
  console.log(`npm package staged: ${columns} column files, ${files.length} plain files; data ${manifest.snapshot.dataFingerprint}`);
} finally {
  await rm(stage, { recursive: true, force: true });
  await rm(home, { recursive: true, force: true });
}
