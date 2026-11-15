// Export every Tongval item to JSON for the renderer: node tools/export_items.js > build/items.json
const path = require('path'), os = require('os');
const D = path.join(os.homedir(), 'tongval/web/data');
global.window = {};
for (const f of ['units_1', 'units_2', 'units_3', 'lesson0', 'stories']) require(path.join(D, f + '.js'));
const out = [], seen = new Set();
const add = (key, o) => { if (seen.has(key)) return; seen.add(key); out.push(Object.assign({ key }, o)); };
for (const u of window.COURSE.sort((a, b) => a.n - b.n)) {
  for (const w of u.words) add('w:' + w.nl + '|' + w.en, { kind: 'w', unit: u.n, nl: w.nl, en: w.en });
  for (const s of u.sentences) add('s:' + s.nl, { kind: 's', unit: u.n, nl: s.nl, en: s.en });
  for (const d of u.dialog) add('d:' + d.nl, { kind: 'd', unit: u.n, nl: d.nl, en: d.en });
}
for (const p of window.LESSON0.parts) for (const c of p.cards) for (const [nl, en] of c.words) add('w0:' + nl, { kind: 'w0', unit: 0, nl, en });
for (const st of window.STORIES) st.lines.forEach((l, i) => add('t:' + st.id + ':' + i, { kind: 't', unit: st.after, nl: l.nl, en: l.en }));
process.stdout.write(JSON.stringify(out));
