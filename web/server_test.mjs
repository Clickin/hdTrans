import assert from 'node:assert/strict';
import { once } from 'node:events';
import { mkdir, mkdtemp, rm, symlink, utimes, writeFile } from 'node:fs/promises';
import { request } from 'node:http';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { matchesIfNoneMatch, strongEtag } from './etag.mjs';
import { createStaticServer } from './server.mjs';

assert.equal(strongEtag(Buffer.from('abc')), '"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"');
assert.equal(matchesIfNoneMatch('W/"opaque,tag", "other"', '"opaque,tag"'), true);
assert.equal(matchesIfNoneMatch('"opaque", "tag"', '"opaque,tag"'), false);
assert.equal(matchesIfNoneMatch('"opaque,tag" garbage', '"opaque,tag"'), false);
assert.equal(matchesIfNoneMatch('W/"opaque,tag"', 'W/"opaque,tag"'), true);
assert.equal(matchesIfNoneMatch('*, "other"', '"other"'), false);
assert.equal(matchesIfNoneMatch(undefined, '"other"'), false);

const temporary = await mkdtemp(join(tmpdir(), 'hdtrans-static-'));
let server;
try {
  const root = join(temporary, 'public');
  await mkdir(join(root, 'empty'), { recursive: true });
  await mkdir(join(root, 'nested'));
  await writeFile(join(root, 'index.html'), 'first');
  await writeFile(join(root, 'nested', 'index.html'), 'nested');
  await writeFile(join(root, 'module.wasm'), Buffer.from([0, 97, 115, 109]));
  await writeFile(join(root, 'app.js'), 'export {};');
  await writeFile(join(root, 'manifest.json'), '{}');
  await writeFile(join(root, 'style.css'), 'body {}');
  const outside = join(temporary, 'secret.txt');
  await writeFile(outside, 'secret');
  await symlink(outside, join(root, 'escape'));
  await symlink(temporary, join(root, 'escape-directory'));
  await symlink(join(root, 'index.html'), join(root, 'inside.html'));
  await symlink(outside, join(root, 'empty', 'index.html'));
  await mkdir(join(root, 'no-index'));
  server = await createStaticServer(root);
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const get = (path, headers = {}, method = 'GET') => new Promise((resolve, reject) => {
    const req = request({ hostname: '127.0.0.1', port: server.address().port, path, headers, method }, (res) => {
      const chunks = [];
      res.on('data', (chunk) => chunks.push(chunk));
      res.on('error', reject);
      res.on('end', () => resolve({ status: res.statusCode, headers: res.headers, body: Buffer.concat(chunks) }));
    });
    req.on('error', reject);
    req.end();
  });
  const fixedTime = new Date('2020-01-01T00:00:00Z');
  await utimes(join(root, 'index.html'), fixedTime, fixedTime);
  const first = await get('/');
  assert.equal(first.status, 200);
  assert.equal(first.body.toString(), 'first');
  assert.equal(first.headers.etag, strongEtag(first.body));
  assert.equal(first.headers['content-type'], 'text/html; charset=utf-8');
  assert.equal(first.headers['content-encoding'], undefined);
  assert.equal(first.headers['cache-control'], 'no-cache');
  for (const condition of [first.headers.etag, `W/${first.headers.etag}`, `"other,tag", W/${first.headers.etag}`, '*']) {
    const cached = await get('/', { 'If-None-Match': condition });
    assert.equal(cached.status, 304);
    assert.equal(cached.headers.etag, first.headers.etag);
    assert.equal(cached.body.length, 0);
  }
  const mismatched = await get('/', { 'If-None-Match': '"not-current"' });
  assert.equal(mismatched.status, 200);
  assert.equal(mismatched.body.toString(), 'first');
  const head = await get('/', {}, 'HEAD');
  assert.equal(head.status, 200);
  assert.equal(head.headers.etag, first.headers.etag);
  assert.equal(head.headers['content-length'], '5');
  assert.equal(head.body.length, 0);
  const cachedHead = await get('/', { 'If-None-Match': `W/${first.headers.etag}` }, 'HEAD');
  assert.equal(cachedHead.status, 304);
  assert.equal(cachedHead.headers.etag, first.headers.etag);
  assert.equal(cachedHead.body.length, 0);
  await writeFile(join(root, 'index.html'), 'later');
  await utimes(join(root, 'index.html'), fixedTime, fixedTime);
  const changed = await get('/', { 'If-None-Match': first.headers.etag });
  assert.equal(changed.status, 200);
  assert.equal(changed.body.toString(), 'later');
  assert.notEqual(changed.headers.etag, first.headers.etag);
  assert.equal(changed.headers.etag, strongEtag(changed.body));
  assert.equal((await get('/inside.html')).body.toString(), 'later');
  assert.equal((await get('/nested/')).body.toString(), 'nested');
  for (const [path, type] of [
    ['/module.wasm', 'application/wasm'],
    ['/app.js', 'text/javascript; charset=utf-8'],
    ['/manifest.json', 'application/json; charset=utf-8'],
    ['/style.css', 'text/css; charset=utf-8'],
  ]) {
    const response = await get(path, { 'Accept-Encoding': 'gzip, br' });
    assert.equal(response.status, 200);
    assert.equal(response.headers['content-type'], type);
    assert.equal(response.headers['content-encoding'], undefined);
    assert.equal(response.headers.etag, strongEtag(response.body));
  }
  for (const path of ['/missing', '/no-index/', '/api/translate']) {
    assert.equal((await get(path, { 'If-None-Match': '*' })).status, 404);
  }
  assert.equal((await get('/api/translate', {}, 'POST')).status, 404);
  for (const path of ['/../secret.txt', '/%2e%2e/secret.txt', '/nested/%2e%2e%2findex.html', '/%5c..%5csecret.txt', '/escape', '/escape-directory/secret.txt', '/empty/']) {
    const rejected = await get(path);
    assert.equal(rejected.status, 403, path);
    assert.equal(rejected.body.length, 0);
  }
  for (const path of ['/%ZZ', '/%E0%A4', '/%00', '//outside.test/path', '/index.html#fragment']) {
    const rejected = await get(path);
    assert.equal(rejected.status, 400, path);
    assert.equal(rejected.body.length, 0);
  }
  assert.equal((await get('/?cache=1')).status, 200);
  console.log('static server: HTTP cache and path safety checks passed');
} finally {
  if (server?.listening) await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  await rm(temporary, { recursive: true, force: true });
}
