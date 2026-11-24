// Checks the display's drum detector (display/web/drums-worklet.js) on the same made-up
// beat as tests/drum_hits_test.cpp: run that with DUMP=<file> first.
// Usage: node tests/drum_hits_js_test.js <beat.f32>
const fs = require('fs');
const { DrumHits } = require('../display/web/drums-worklet.js');
const file = process.argv[2];
const buf = fs.readFileSync(file);
const x = new Float32Array(buf.buffer, buf.byteOffset, buf.length / 4);
const truth = fs.readFileSync(file + '.hits', 'utf8').trim().split('\n').map((l) => l.split(' ').map(Number));
const sr = 48000, tol = 0.03 * sr, names = ['KICK', 'SNARE', 'PERC', 'HATS'];
for (const gain of [1, 0.1]) {
  const d = new DrumHits(sr), last = [0, 0, 0, 0], found = [];
  for (let i = 0; i < x.length; i++) {
    d.run(x[i] * gain);
    for (let l = 0; l < 4; l++) if (d.count[l] !== last[l]) { last[l] = d.count[l]; found.push([l, i]); }
  }
  let failures = 0;
  for (let l = 0; l < 4; l++) {
    const real = truth.filter((t) => t[0] === l), mine = found.filter((f) => f[0] === l);
    const near = (a, b) => a + tol / 3 >= b && a <= b + tol;
    const caught = real.filter((t) => mine.some((f) => near(f[1], t[1]))).length;
    const extra = mine.filter((f) => !real.some((t) => near(f[1], t[1]))).length;
    const pass = caught >= Math.floor(real.length * 9 / 10) && extra <= Math.floor(real.length / 4);
    console.log(`${pass ? 'PASS' : 'FAIL'}  gain ${gain}  ${names[l].padEnd(5)} caught ${caught} of ${real.length}, ${extra} false`);
    if (!pass) failures++;
  }
  if (failures) { console.log('display drum detection FAILED'); process.exit(1); }
}
console.log('display drum detection: all lanes pass');
