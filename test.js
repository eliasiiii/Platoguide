// node test.js <path to pool/game/main.js>
const fs = require('fs'), vm = require('vm'), cp = require('child_process'), assert = require('assert');
const ctx = {}; vm.createContext(ctx);
vm.runInContext(fs.readFileSync(__dirname + '/pg_patch.js', 'utf8'), ctx);
const main = fs.readFileSync(process.argv[2], 'utf8');
const res = ctx.pgPatch(main);
console.log('patch ok:', res.ok, res.reason);
assert(res.ok);
fs.writeFileSync('/tmp/main.patched.js', res.src);
cp.execSync('node --check /tmp/main.patched.js'); console.log('patched file passes node --check');
assert(!ctx.pgPatch(res.src).ok); console.log('second pass refused (idempotent)');

const W = (/pos:\[([\w$]+)-49,47\]/.exec(main) || [])[1], H = (/pos:\[49,([\w$]+)-47\]/.exec(main) || [])[1];
function arr(marker) {
  const idx = main.indexOf(marker); assert(idx > 0, 'marker not found: ' + marker);
  const i = main.lastIndexOf('=[{', idx) + 1;
  let d = 0, j = i;
  for (; j < main.length; j++) { if (main[j] === '[') d++; else if (main[j] === ']' && --d === 0) break; }
  return new Function(W, H, 'return (' + main.slice(i, j + 1) + ')')(834, 1516);
}
const rails = arr('pos:[0,116],path:'), jaws = arr('pos:[0,0],path:[[0,0],[115,0],[115,58]'), pk = arr('pos:[49,47],radius:70');
const body = (t) => ({ position: t.pos.slice(), concavePath: t.path });
const world = { bodies: [...rails, ...jaws].map(body) };
const table = { pockets: pk.map(p => ({ position: p.pos, radius: p.radius })), rails: [] };
const draw = ctx.__pgRuntime(), R = 20;
const segs = draw._collect(world, table), P = draw._trace;
console.log('segments:', segs.length);
let r = P(417, 1100, 0, -1, R, segs, [], table.pockets, 0);
console.log('straight up ->', r.pts[1].map(v => +v.toFixed(2)), r.end);
assert(Math.abs(r.pts[1][1] - 97) < 0.01);
const L = Math.hypot(320, -100); r = P(417, 1100, 320 / L, -100 / L, R, segs, [], table.pockets, 1);
console.log('right cushion first bounce ->', r.pts[1].map(v => +v.toFixed(2)));
assert(Math.abs(r.pts[1][0] - 737) < 0.01);
r = P(417, 1100, 0, -1, R, segs, [{ position: [417, 600] }], table.pockets, 4);
console.log('ball hit contact ->', r.contact.point.map(v => +v.toFixed(2)), r.end);
assert(Math.abs(r.contact.point[1] - 640) < 0.01);
r = P(417, 1100, Math.cos(-1.2), Math.sin(-1.2), R, segs, [], table.pockets, 4);
console.log('4 bounces ->', r.pts.length - 1, 'points, end:', r.end);

// draw() config test with a fake game object
function run(cfg) {
  ctx.__pgCfg = cfg;
  const calls = [];
  const node = { logic: { isLocalPlayerTurn: () => true, areAimLinesVisible: () => true, world,
    table: { pockets: table.pockets, rails: [], cueBall: { radius: R, position: [417, 1100] },
      balls: [{ position: [430, 600], isPocketed: () => false }] } } };
  draw(0, node, { strokeLines: (t, pts) => calls.push(pts.length) }, { visible: true, width: 50, rotation: -Math.PI / 2 });
  return calls.length;
}
ctx.__pgCfg = undefined;
console.log('default config draws', run(undefined), 'polylines'); assert.strictEqual(run(undefined), 3);
assert.strictEqual(run({ guide: 0 }), 0);
assert.strictEqual(run({ guide: 1, obj: 0, cue: 1, bounces: 4 }), 2);
assert.strictEqual(run({ guide: 1, obj: 0, cue: 0, bounces: 4 }), 1);
console.log('menu config checks passed');
console.log('ALL TESTS PASSED');
