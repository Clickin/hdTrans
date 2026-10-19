import { access, readFile } from 'node:fs/promises';

const manifest = JSON.parse(await readFile(new URL('./dist/dictionary.json', import.meta.url), 'utf8'));
for (const name of [
  'dist/index.js', 'dist/index.d.ts', 'dist/cli.js', 'dist/cli.d.ts',
  'dist/runtime.js', 'dist/runtime.d.ts',
  'dist/hdTransEngine.js', 'dist/hdTransEngine.wasm',
  'dist/dicts.hdsnap', 'dist/dicts.txt', 'LICENSE', 'THIRD-PARTY-NOTICES.txt',
  ...manifest.files.map((file) => `dist/${file}`),
]) {
  await access(new URL(name, import.meta.url));
}
