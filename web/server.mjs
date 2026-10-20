import { readFile, realpath, stat } from 'node:fs/promises';
import { createServer } from 'node:http';
import { extname, isAbsolute, join, relative, resolve, sep } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { matchesIfNoneMatch, strongEtag } from './etag.mjs';

const defaultDirectory = fileURLToPath(new URL('../build/web/dist/', import.meta.url));
const mime = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.wasm': 'application/wasm',
  '.txt': 'text/plain; charset=utf-8',
  '.tsv': 'text/tab-separated-values; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.gif': 'image/gif',
  '.webp': 'image/webp',
  '.ico': 'image/x-icon',
  '.woff': 'font/woff',
  '.woff2': 'font/woff2',
};

export async function createStaticServer(directory = defaultDirectory) {
  const root = await realpath(directory);
  if (!(await stat(root)).isDirectory()) throw new Error('Static root must be a directory');
  const insideRoot = (file) => {
    const path = relative(root, file);
    return path !== '..' && !path.startsWith(`..${sep}`) && !isAbsolute(path);
  };
  return createServer(async (request, response) => {
    const finish = (status) => {
      response.writeHead(status, { 'Content-Length': '0' });
      response.end();
    };
    let pathname;
    try {
      // Inspect the raw path before URL dot-segment normalization can erase traversal.
      if (!request.url.startsWith('/') || request.url.startsWith('//') || request.url.includes('#')) {
        return finish(400);
      }
      decodeURI(request.url);
      pathname = decodeURIComponent(request.url.split('?')[0]);
      if (/[\x00-\x1f\x7f]/.test(pathname)) return finish(400);
    } catch {
      return finish(400);
    }
    if (pathname.includes('\\') || pathname.split('/').includes('..')) return finish(403);
    if (request.method !== 'GET' && request.method !== 'HEAD') return finish(404);
    try {
      let file = await realpath(resolve(root, `.${pathname}`));
      if (!insideRoot(file)) return finish(403);
      if ((await stat(file)).isDirectory()) {
        file = await realpath(join(file, 'index.html'));
        if (!insideRoot(file)) return finish(403);
      }
      if (!(await stat(file)).isFile()) return finish(404);
      // Hash and send the same buffer; mtime/size cannot detect every content change.
      const bytes = await readFile(file);
      const etag = strongEtag(bytes);
      response.setHeader('ETag', etag);
      response.setHeader('Cache-Control', 'no-cache');
      response.setHeader('X-Content-Type-Options', 'nosniff');
      if (matchesIfNoneMatch(request.headers['if-none-match'], etag)) {
        response.writeHead(304);
        return response.end();
      }
      response.writeHead(200, {
        'Content-Type': mime[extname(file).toLowerCase()] ?? 'application/octet-stream',
        'Content-Length': bytes.length,
      });
      response.end(request.method === 'HEAD' ? undefined : bytes);
    } catch (error) {
      if (['ENOENT', 'ENOTDIR', 'ELOOP'].includes(error.code)) return finish(404);
      if (['EACCES', 'EPERM'].includes(error.code)) return finish(403);
      finish(500);
    }
  });
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const [directory = defaultDirectory, portText = '8000', ...extra] = process.argv.slice(2);
  const port = Number(portText);
  if (extra.length || !/^\d+$/.test(portText) || !Number.isInteger(port) || port < 0 || port > 65535) {
    console.error('Usage: node web/server.mjs [directory] [port]');
    process.exitCode = 1;
  } else {
    try {
      const server = await createStaticServer(directory);
      server.on('error', () => {
        console.error('Unable to start static server');
        process.exitCode = 1;
      });
      server.listen(port, '127.0.0.1', () => {
        console.log(`http://127.0.0.1:${server.address().port}`);
      });
    } catch {
      console.error('Unable to open static directory');
      process.exitCode = 1;
    }
  }
}
