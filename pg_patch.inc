static const char* kPatcherSrc = R"PGJS(
// pg_patch.js - source of truth. Tested in node, executed by the dylib inside a private JSContext.
// Draws extended aim lines (cushion bounces) inside the game's own renderAimLines().
function __pgRuntime() {
  var DEBUG_KEY = '__pgDebug';
  var MAX_BOUNCES = 4, MAX_LEN = 5000;

  function rayCircle(px, py, dx, dy, cx, cy, r) {
    var fx = px - cx, fy = py - cy;
    var b = fx * dx + fy * dy, c = fx * fx + fy * fy - r * r, disc = b * b - c;
    if (disc < 0) return null;
    var t = -b - Math.sqrt(disc);
    return t >= -0.5 ? t : null;
  }

  // Circle of radius R moving from (px,py) along unit (dx,dy) against segment s=[ax,ay,bx,by].
  function sweepSeg(px, py, dx, dy, R, s) {
    var ax = s[0], ay = s[1], bx = s[2], by = s[3];
    var ex = bx - ax, ey = by - ay, el = Math.sqrt(ex * ex + ey * ey);
    if (el < 1e-6) return null;
    var nx = -ey / el, ny = ex / el;
    var side = (px - ax) * nx + (py - ay) * ny;
    if (side < 0) { nx = -nx; ny = -ny; side = -side; }
    var best = null, vn = dx * nx + dy * ny;
    if (vn < -1e-9) {
      var t = (side - R) / (-vn);
      if (t >= -0.5) {
        var hx = px + dx * t, hy = py + dy * t;
        var u = ((hx - ax) * ex + (hy - ay) * ey) / (el * el);
        if (u >= 0 && u <= 1) best = { t: Math.max(t, 0), nx: nx, ny: ny };
      }
    }
    var ends = [[ax, ay], [bx, by]];
    for (var k = 0; k < 2; k++) {
      var tv = rayCircle(px, py, dx, dy, ends[k][0], ends[k][1], R);
      if (tv === null) continue;
      var qx = px + dx * tv - ends[k][0], qy = py + dy * tv - ends[k][1];
      var ql = Math.sqrt(qx * qx + qy * qy) || 1;
      if (dx * qx + dy * qy < 0 && (!best || tv < best.t)) best = { t: Math.max(tv, 0), nx: qx / ql, ny: qy / ql };
    }
    return best;
  }

  function collectSegs(world, table) {
    var segs = [], bodies = (world && world.bodies) || [], i, j;
    var list = [];
    for (i = 0; i < bodies.length; i++) if (bodies[i].concavePath && bodies[i].concavePath.length > 1) list.push(bodies[i]);
    if (list.length === 0 && table.rails) list = table.rails.filter(function (b) { return b.concavePath; });
    for (i = 0; i < list.length; i++) {
      var cp = list[i].concavePath, p = list[i].position;
      for (j = 0; j < cp.length; j++) {
        var a = cp[j], c = cp[(j + 1) % cp.length];
        segs.push([p[0] + a[0], p[1] + a[1], p[0] + c[0], p[1] + c[1]]);
      }
    }
    return segs;
  }

  function trace(px, py, dx, dy, R, segs, targets, pockets, maxB) {
    var pts = [[px, py]], total = 0, contact = null, end = 'len';
    for (var b = 0; b <= maxB; b++) {
      var tMin = MAX_LEN - total, kind = 0, nx = 0, ny = 0, ball = null, i;
      for (i = 0; i < segs.length; i++) {
        var h = sweepSeg(px, py, dx, dy, R, segs[i]);
        if (h && h.t < tMin) { tMin = h.t; kind = 1; nx = h.nx; ny = h.ny; }
      }
      for (i = 0; i < targets.length; i++) {
        var g = targets[i], t2 = rayCircle(px, py, dx, dy, g.position[0], g.position[1], 2 * R);
        if (t2 !== null && t2 > 1e-6 && t2 < tMin) { tMin = t2; kind = 2; ball = g; }
      }
      for (i = 0; i < pockets.length; i++) {
        var pk = pockets[i], t3 = rayCircle(px, py, dx, dy, pk.position[0], pk.position[1], pk.radius - R);
        if (t3 !== null && t3 < tMin) { tMin = Math.max(t3, 0); kind = 3; }
      }
      if (tMin < 0) tMin = 0;
      px += dx * tMin; py += dy * tMin; total += tMin; pts.push([px, py]);
      if (kind === 0) { end = 'len'; break; }
      if (kind === 3) { end = 'pocket'; break; }
      if (kind === 2) {
        var ox = ball.position[0] - px, oy = ball.position[1] - py, ol = Math.sqrt(ox * ox + oy * oy) || 1;
        ox /= ol; oy /= ol;
        var dd = dx * ox + dy * oy, tx = dx - dd * ox, ty = dy - dd * oy, tl = Math.sqrt(tx * tx + ty * ty);
        contact = { ball: ball, point: [px, py], obj: [ox, oy], cue: tl > 0.02 ? [tx / tl, ty / tl] : null };
        end = 'ball'; break;
      }
      var dn = dx * nx + dy * ny; dx -= 2 * dn * nx; dy -= 2 * dn * ny;
      var l = Math.sqrt(dx * dx + dy * dy) || 1; dx /= l; dy /= l;
      if (b === maxB) end = 'bounces';
    }
    return { pts: pts, contact: contact, end: end };
  }

  var S_MAIN = { antialias: 1, color: [1, 1, 1, 0.9], colorShadow: [0, 0, 0, 0.35], miterLimit: 1, width: 3 };
  var S_OBJ = { antialias: 1, color: [1, 0.85, 0.2, 0.9], colorShadow: [0, 0, 0, 0.35], miterLimit: 1, width: 3 };
  var S_CUE = { antialias: 1, color: [0.3, 0.85, 1, 0.9], colorShadow: [0, 0, 0, 0.35], miterLimit: 1, width: 3 };
  var S_DBG = { antialias: 1, color: [1, 0, 0, 0.8], miterLimit: 1, width: 2 };

  function draw(t, node, lines, main) {
    try {
      var lg = node.logic;
      if (!lg || !main || !main.visible || !(main.width > 0)) return;
      if (typeof lg.isLocalPlayerTurn === 'function' && !lg.isLocalPlayerTurn()) return;
      if (typeof lg.areAimLinesVisible === 'function' && lg.areAimLinesVisible() === false) return;
      var cfg = (typeof __pgCfg === 'object' && __pgCfg) ? __pgCfg : {};
      if (cfg.guide === 0 || cfg.guide === false) return;
      var nb = (typeof cfg.bounces === 'number' && cfg.bounces >= 0 && cfg.bounces <= 8) ? Math.floor(cfg.bounces) : MAX_BOUNCES;
      var showObj = !(cfg.obj === 0 || cfg.obj === false), showCue = !(cfg.cue === 0 || cfg.cue === false);
      var table = lg.table, cue = table.cueBall, R = cue.radius;
      var ang = main.rotation;
      if (!(R > 0) || !isFinite(ang)) return;
      var segs = collectSegs(lg.world, table), pockets = table.pockets || [], targets = [], i;
      for (i = 0; i < table.balls.length; i++) if (!table.balls[i].isPocketed()) targets.push(table.balls[i]);
      var r1 = trace(cue.position[0], cue.position[1], Math.cos(ang), Math.sin(ang), R, segs, targets, pockets, nb);
      lines.strokeLines(t, r1.pts, S_MAIN);
      if (r1.contact && (showObj || showCue)) {
        var c = r1.contact, rest = targets.filter(function (b) { return b !== c.ball; });
        if (showObj) {
          var ro = trace(c.ball.position[0], c.ball.position[1], c.obj[0], c.obj[1], R, segs, rest, pockets, Math.min(nb, 2));
          lines.strokeLines(t, ro.pts, S_OBJ);
        }
        if (showCue && c.cue) {
          var rc = trace(c.point[0], c.point[1], c.cue[0], c.cue[1], R, segs, rest, pockets, Math.min(nb, 1));
          lines.strokeLines(t, rc.pts, S_CUE);
        }
      }
      if (lg[DEBUG_KEY]) for (i = 0; i < segs.length; i++) lines.strokeLines(t, [[segs[i][0], segs[i][1]], [segs[i][2], segs[i][3]]], S_DBG);
    } catch (e) { }
  }
  draw._trace = trace; draw._collect = collectSegs;
  return draw;
}

function pgPatch(src) {
  function fail(r) { return { ok: false, reason: r, src: src }; }
  if (typeof src !== 'string') return fail('not a string');
  if (src.indexOf('renderAimLines(') < 0) return fail('no renderAimLines anchor');
  if (src.indexOf('__pgDraw') >= 0) return fail('already patched');
  var m = /static registerLinesShader\(\)\{return ([\w$]+)===void 0/.exec(src);
  if (!m) return fail('lines shader variable not found');
  var lines = m[1];
  m = /var ([\w$]+)=class extends [\w$]+\{constructor\([\w$]+,[\w$]+\)\{super\(0,[^;]{1,40}\);this\.onUpdated=new/.exec(src);
  if (!m) return fail('aim line class not found');
  var cls = m[1].replace(/\$/g, '\\$');
  m = new RegExp('([\\w$]+)=new ' + cls + '\\([\\w$]+,[\\w$]+\\),\\1\\.visible=!1').exec(src);
  if (!m) return fail('aim line instance not found');
  var main = m[1];
  var ra = /renderAimLines\(([\w$]+)\)\{/.exec(src);
  if (!ra) return fail('renderAimLines definition not found');
  var at = src.indexOf('(()=>{');
  if (at < 0 || at > 40) return fail('bundle prologue not found');
  var call = '__pgDraw(' + ra[1] + ',this,' + lines + ',' + main + ');';
  var out = src.replace(ra[0], function () { return ra[0] + call; });
  var runtime = 'var __pgDraw=(' + __pgRuntime.toString() + ')();';
  out = out.slice(0, at + 6) + runtime + out.slice(at + 6);
  return { ok: true, reason: 'lines=' + lines + ' main=' + main, src: out };
}
)PGJS";
