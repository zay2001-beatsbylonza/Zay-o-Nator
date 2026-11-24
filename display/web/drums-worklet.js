// ZaY-o-Nator Display: drum hit detection for the DRUMS face, on the browser's audio thread.
// By BeatsbyLonza. MIT licence.
//
// The same detector as the DAW plugin (daw-plugin/DrumHits.hpp), sample by sample:
//   KICK low-pass ~110 Hz, SNARE band-pass ~3 kHz (crack) with ~200 Hz (body),
//   PERC band-pass ~800 Hz, HATS high-pass ~9 kHz.
// A lane hits when its fast envelope jumps well above its own slow average, with
// cross-checks so a kick's click isn't a snare and a snare's top isn't a hat.
// It posts the hit counts [kick, snare, perc, hats] whenever one changes.

const KICK = 0, SNARE = 1, PERC = 2, HATS = 3;

class Biquad {
  constructor(type, sr, f, q) {
    const w = 2 * Math.PI * f / sr, cw = Math.cos(w), alpha = Math.sin(w) / (2 * q), a0 = 1 + alpha;
    let b0, b1, b2;
    if (type === 'lp') { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; }
    else if (type === 'hp') { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; }
    else { b0 = alpha; b1 = 0; b2 = -alpha; }
    this.b0 = b0 / a0; this.b1 = b1 / a0; this.b2 = b2 / a0; this.a1 = -2 * cw / a0; this.a2 = (1 - alpha) / a0;
    this.z1 = 0; this.z2 = 0;
  }
  run(x) {
    const y = this.b0 * x + this.z1;
    this.z1 = this.b1 * x - this.a1 * y + this.z2;
    this.z2 = this.b2 * x - this.a2 * y;
    return y;
  }
}

class Env {
  constructor(sr, attackMs, releaseMs) {
    this.v = 0;
    this.att = 1 - Math.exp(-1 / (sr * attackMs * 0.001));
    this.rel = 1 - Math.exp(-1 / (sr * releaseMs * 0.001));
  }
  run(x) { x = Math.abs(x); this.v += (x > this.v ? this.att : this.rel) * (x - this.v); return this.v; }
}

class DrumHits {
  constructor(sr) {
    this.sr = sr;
    this.count = [0, 0, 0, 0];
    this.kick = [new Biquad('lp', sr, 110, 0.7), new Biquad('lp', sr, 110, 0.7)];
    this.snareHi = new Biquad('bp', sr, 3000, 0.8);
    this.snareLo = new Biquad('bp', sr, 200, 1.0);
    this.perc = new Biquad('bp', sr, 800, 1.2);
    this.hat = [new Biquad('hp', sr, 9000, 0.7), new Biquad('hp', sr, 9000, 0.7)];
    this.fast = []; this.slow = [];
    for (let i = 0; i < 5; i++) { this.fast.push(new Env(sr, 0.5, 12)); this.slow.push(new Env(sr, 60, 250)); }
    this.since = [1 << 30, 1 << 30, 1 << 30, 1 << 30];
    this.pending = [0, 0, 0, 0];
    this.ms = Math.floor(sr / 1000);
    this.kickAt = 0; this.percNear = false;
    this.crackMax = 0; this.lowMax = 0; this.topMax = 0; this.hatTop = 0;
    this.f = new Float32Array(5); this.s = new Float32Array(5);
  }
  hit(lane) {
    this.since[lane] = 0;
    if ((lane === KICK || lane === SNARE) && this.pending[PERC] > 0) this.percNear = true;
    this.count[lane]++;
  }
  run(mono) {
    const x0 = this.kick[1].run(this.kick[0].run(mono)), x1 = this.snareHi.run(mono), x2 = this.perc.run(mono);
    const x3 = this.hat[1].run(this.hat[0].run(mono)), x4 = this.snareLo.run(mono);
    const f = this.f, s = this.s, xs = [x0, x1, x2, x3, x4];
    for (let i = 0; i < 5; i++) { f[i] = this.fast[i].run(xs[i]); s[i] = this.slow[i].run(xs[i]); }
    const since = this.since, pending = this.pending, ms = this.ms;
    for (let l = 0; l < 4; l++) { since[l]++; if (pending[l] > 0) pending[l]--; }
    const onset = (fa, sl, ratio, floor) => fa > floor && fa > sl * ratio;

    if (onset(f[0], s[0], 2.5, 0.03) && f[0] > f[2] * 1.2 && since[KICK] > 90 * ms) this.hit(KICK);

    if (!pending[SNARE] && since[SNARE] > 80 * ms && onset(f[1], s[1], 2.5, 0.008) && f[4] > s[4] * 1.8 && f[4] > 0.01) {
      pending[SNARE] = 10 * ms; this.crackMax = 0; this.lowMax = 0; this.topMax = 0; this.kickAt = since[KICK];
    }
    if (pending[SNARE] > 0) {
      this.crackMax = Math.max(this.crackMax, f[1]); this.lowMax = Math.max(this.lowMax, f[0]); this.topMax = Math.max(this.topMax, f[3]);
    }
    if (!pending[HATS] && since[HATS] > 45 * ms && onset(f[3], s[3], 2.2, 0.003) && f[3] > f[1] * 0.35) {
      pending[HATS] = 10 * ms; this.hatTop = f[3] / (f[1] + 1e-9);
    }
    if (!pending[PERC] && since[PERC] > 70 * ms && onset(f[2], s[2], 2.8, 0.008)) {
      pending[PERC] = 10 * ms; this.percNear = since[KICK] < 40 * ms || since[SNARE] < 40 * ms;
    }

    if (pending[SNARE] === 1) {
      const kickNow = this.kickAt < 45 * ms || since[KICK] < 11 * ms;
      if (!kickNow || (this.crackMax > 0.2 * this.lowMax && this.crackMax > 0.8 * this.topMax)) this.hit(SNARE);
    }
    if (pending[HATS] === 1 && (this.hatTop > 1.6 || (since[SNARE] > 26 * ms && pending[SNARE] === 0))) this.hit(HATS);
    if (pending[PERC] === 1 && pending[SNARE] > 1) pending[PERC] = pending[SNARE] + 1;
    if (pending[PERC] === 1 && !this.percNear && since[KICK] > 26 * ms && since[SNARE] > 26 * ms) this.hit(PERC);
  }
}

if (typeof AudioWorkletProcessor !== 'undefined') {
  class DrumHitsProcessor extends AudioWorkletProcessor {
    constructor() { super(); this.d = new DrumHits(sampleRate); this.sent = '0,0,0,0'; }
    process(inputs) {
      const ch = inputs[0];
      if (ch && ch.length) {
        const a = ch[0], b = ch[1] || ch[0];
        for (let i = 0; i < a.length; i++) this.d.run(0.5 * (a[i] + b[i]));
        const now = this.d.count.join(',');
        if (now !== this.sent) { this.sent = now; this.port.postMessage(this.d.count.slice()); }
      }
      return true;
    }
  }
  registerProcessor('zay-drum-hits', DrumHitsProcessor);
} else if (typeof module !== 'undefined') {
  module.exports = { DrumHits };   // for the test in tests/
}
