import assert from 'node:assert/strict';
import { existsSync, mkdtempSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';

const root = fileURLToPath(new URL('../../', import.meta.url));
const build = readFileSync(join(root, '.github/workflows/build.yml'), 'utf8');
const start = build.indexOf('      - name: Compare with the expected outputs\n');
assert.notEqual(start, -1);
const end = build.indexOf('\n      - name:', start + 1);
const comparison = build.slice(start, end === -1 ? undefined : end);
const script = comparison.match(/        run: \|\n([\s\S]*)$/)[1].replace(/^ {10}/gm, '');

test('expected-data comparison belongs to the existing Linux build', () => {
  assert.equal(existsSync(join(root, '.github/workflows/e2e.yml')), false);
  assert.match(comparison, /if: matrix\.preset == 'linux' && env\.HAS_TESTDATA_KEY == 'true'/);
  assert.match(script, /tools\/e2e\.sh --expected testdata build\/linux\/hdtrans/);
  assert.doesNotMatch(script, /cmake|createWorkflowDispatch/);
  assert.match(build, /needs: \[build, release-tools\]/);
});

for (const status of [0, 1, 2]) {
  test(`expected-data step preserves exit policy and private logs: ${status}`, t => {
    const dir = mkdtempSync(join(tmpdir(), 'hdtrans-ci-expected-'));
    t.after(() => rmSync(dir, { recursive: true, force: true }));
    mkdirSync(join(dir, 'tools'));
    mkdirSync(join(dir, 'dicts'));
    writeFileSync(join(dir, 'tools/e2e.sh'), `#!/bin/sh
[ "$1" = --expected ] && [ "$2" = testdata ] && [ "$3" = build/linux/hdtrans ] || exit 2
printf '%s\n' 'fixture: 1 changed line' '  1 A: fixture-private-marker' 'e2e: 1 documents compared, 1 changed lines in 1 documents'
exit "$FIXTURE_STATUS"
`, { mode: 0o700 });
    const summary = join(dir, 'summary');
    const result = spawnSync('bash', ['--noprofile', '--norc', '-e', '-o', 'pipefail', '-c', script], {
      cwd: dir,
      env: { ...process.env, RUNNER_TEMP: dir, GITHUB_WORKSPACE: dir, GITHUB_STEP_SUMMARY: summary, FIXTURE_STATUS: String(status) },
      encoding: 'utf8',
    });
    assert.ifError(result.error);
    assert.equal(result.status, status <= 1 ? 0 : 1, result.stderr);
    assert.doesNotMatch(result.stdout + result.stderr, /fixture-private-marker/);
    assert.match(readFileSync(join(dir, 'e2e.log'), 'utf8'), /fixture-private-marker/);
    const report = readFileSync(summary, 'utf8');
    assert.doesNotMatch(report, /fixture-private-marker/);
    if (status === 0) assert.match(report, /No translation changed/);
    if (status === 1) assert.match(report, /Translations changed; accept them/);
    assert.deepEqual(readdirSync(dir).filter(name => name.startsWith('hdtrans-e2e-home.')), []);
  });
}
