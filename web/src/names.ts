/**
 * Returns a names table as a plain dictionary: a line without a part of
 * speech gets name, and a line without a tab is split at its first run of
 * spaces.
 */
export function namesDict(text: string): string {
  return text
    .split('\n')
    .map((line) => {
      line = line.replace(/\r$/, '');
      const t = line.trim();
      if (t === '' || t.startsWith('//')) return line;
      let f = line.split('\t');
      if (f.length === 1) {
        const i = t.indexOf(' ');
        f = i < 0 ? [t] : [t.slice(0, i), t.slice(i + 1)];
      }
      if (f.length === 2) f.push('name');
      else if (f.length > 2 && f[2].trim() === '') f[2] = 'name';
      return f.join('\t');
    })
    .join('\n');
}
