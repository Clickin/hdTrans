import { createHash } from 'node:crypto';

export function strongEtag(bytes) {
  return `"${createHash('sha256').update(bytes).digest('hex')}"`;
}

// GET/HEAD use weak comparison. Commas inside an opaque tag are not separators.
export function matchesIfNoneMatch(header, etag) {
  if (typeof header !== 'string') return false;
  if (header.trim() === '*') return true;
  const tag = /(?:W\/)?"[\x21\x23-\x7e\x80-\xff]*"/y;
  const current = etag.replace(/^W\//, '');
  let matched = false;
  let index = 0;
  while (index < header.length) {
    while (/[ \t,]/.test(header[index] ?? '') && index < header.length) index++;
    if (index === header.length) break;
    tag.lastIndex = index;
    const value = tag.exec(header);
    if (!value) return false;
    matched ||= value[0].replace(/^W\//, '') === current;
    index = tag.lastIndex;
    while (header[index] === ' ' || header[index] === '\t') index++;
    if (index < header.length && header[index] !== ',') return false;
  }
  return matched;
}
