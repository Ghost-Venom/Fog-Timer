// Fog Timer desk case: Freenove CYD (2.8" ESP32 touch display) + 433 MHz module protoboard.
// Wedge-shaped stand: the screen leans back on the sloped front, and the radio protoboard
// screws to the bottom plate behind it.
//
// Run:  npm install   (once)
//       node fog_case.mjs
// Writes fog_case_shell.stl, fog_case_plate.stl (print-ready) and fog_case_preview.stl.
//
// Measure your parts with calipers and adjust the "Measured parts" values before printing.

import Module from 'manifold-3d';
import { writeFileSync, mkdirSync } from 'node:fs';

const P = {
  // ---- Measured parts ----
  cydW: 86, cydH: 50, pcbT: 1.6,  // CYD circuit board
  cydHoleDx: 79, cydHoleDy: 43,    // CYD mounting-hole centre spacing
  dispStack: 4.8,                  // board front surface to top of the touch glass
  cydBack: 12,                     // tallest part or plug on the CYD's back
  viewW: 58.5, viewH: 44,          // screen window (2.8" active area is 57.6 x 43.2)
  viewFromUsbEdge: 14,             // USB-side board edge to the start of the visible screen
  usbSide: 'left',              // side the USB-C port is on, looking at the screen
  usbW: 13, usbH: 8,               // USB-C plug opening
  pbW: 70, pbH: 30,                // radio protoboard
  pbHoleDx: 66, pbHoleDy: 26,      // protoboard mounting-hole centre spacing
  pbStandoff: 5,
  stylusD: 6.5,                    // stylus holder hole (stylus shaft diameter + ~1 mm)

  // ---- Case ----
  tilt: 65,        // screen angle from the desk
  wall: 2.4,
  frontH: 10,      // vertical lip under the screen
  sideClear: 5,    // space beside the CYD's short edges
  plugClear: 10,   // space beside the CYD's long edges for the JST plugs
  topFlat: 40,     // flat top depth behind the screen
  plateT: 2.4,
};

const wasm = await Module();
wasm.setup();
const { Manifold, CrossSection } = wasm;

const rad = (d) => (d * Math.PI) / 180;
const t = rad(P.tilt);
const slopeLen = P.cydH + 2 * P.plugClear + 2 * P.wall;
const W = P.cydW + 2 * P.sideClear + 2 * P.wall;
const D = slopeLen * Math.cos(t) + P.topFlat;
const H = P.frontH + slopeLen * Math.sin(t);
const standoffLen = P.dispStack + 0.3;  // small gap so the bezel never presses the touch glass
const usbSign = P.usbSide === 'right' ? 1 : -1;
const cx = W / 2, cy = slopeLen / 2;  // CYD centre on the sloped face
const bossR = 3.5, bossH = P.frontH - 2;
const bossPos = [
  [P.wall + bossR, P.wall + bossR], [W - P.wall - bossR, P.wall + bossR],
  [P.wall + bossR, D - P.wall - bossR], [W - P.wall - bossR, D - P.wall - bossR],
];
const pbX = W / 2, pbY = D - P.wall - 3 - P.pbH / 2;
// Stylus holder: in the gap beside the protoboard, back corner on the USB side.
const styX = usbSign > 0 ? W - P.wall - 6.5 : P.wall + 6.5, styY = D - P.wall - 12;

const cube = (s, c = false) => Manifold.cube(s, c);
const cyl = (h, d, d2 = d) => Manifold.cylinder(h, d / 2, d2 / 2, 48);
const union = (list) => Manifold.union(list);

// 2D side profile (Y = depth, Z = height) extruded across the width.
const extrudeX = (pts, w) => new CrossSection([pts]).extrude(w).rotate([90, 0, 90]);

// Coordinates on the sloped face: x across, y up the slope, z out of the face (inside is -z).
const onSlope = (m) => m.rotate([P.tilt, 0, 0]).translate([0, 0, P.frontH]);

function shell() {
  const outer = extrudeX([[0, 0], [D, 0], [D, H], [slopeLen * Math.cos(t), H], [0, P.frontH]], W);
  const inner = extrudeX([
    [P.wall, -1], [D - P.wall, -1], [D - P.wall, H - P.wall],
    [P.wall / Math.sin(t) + (H - P.wall - P.frontH) / Math.tan(t), H - P.wall],
    [P.wall, P.frontH - (P.wall * (1 - Math.sin(t))) / Math.cos(t)],
  ], W - 2 * P.wall).translate([P.wall, 0, 0]);

  const standoffs = [];
  for (const sx of [-1, 1]) for (const sy of [-1, 1]) {
    const s = cyl(standoffLen + 0.5, 5.5).subtract(cyl(standoffLen, 2.5).translate([0, 0, -0.1]));
    standoffs.push(onSlope(s.translate([cx + sx * P.cydHoleDx / 2, cy + sy * P.cydHoleDy / 2, -P.wall - standoffLen])));
  }

  const bosses = bossPos.map(([x, y]) =>
    cyl(bossH, bossR * 2).subtract(cyl(bossH - 1, 2.5).translate([0, 0, -0.1])).translate([x, y, 0]));

  // Screen window with a 45° chamfer on the outside.
  const winX = cx + usbSign * (P.cydW / 2 - P.viewFromUsbEdge - P.viewW / 2);
  const windowCut = onSlope(Manifold.hull([
    cube([P.viewW, P.viewH, 0.01], true).translate([0, 0, -P.wall - 0.1]),
    cube([P.viewW + 2 * P.wall, P.viewH + 2 * P.wall, 0.01], true).translate([0, 0, 0.1]),
  ]).translate([winX, cy, 0]));

  // USB-C opening in the side wall, level with the port on the board's back.
  const usbZ = -(P.wall + standoffLen + P.pcbT + 1.7);
  const usbCut = onSlope(cube([P.wall + P.sideClear + 2, P.usbW, P.usbH]).translate([
    usbSign > 0 ? W - P.wall - P.sideClear - 1 : -1, cy - P.usbW / 2, usbZ - P.usbH / 2]));

  const vents = [];
  for (let i = 0; i < 6; i++)
    vents.push(cube([4, P.wall + 2, H * 0.35]).translate([W / 2 - 23 + i * 9, D - P.wall - 1, H * 0.3]));

  // Stylus holder: a tube hanging from the top, open at the bottom; the stylus hangs by its clip end.
  const tubeLen = H - 2;
  const stylusTube = cyl(tubeLen, P.stylusD + 3.2).translate([styX, styY, 2]);
  const stylusBore = union([
    cyl(H + 2, P.stylusD).translate([styX, styY, 0]),
    cyl(1.5, P.stylusD, P.stylusD + 3).translate([styX, styY, H - 1.49]),  // chamfered mouth
  ]);

  return union([outer.subtract(inner), ...standoffs, ...bosses, stylusTube])
    .subtract(union([windowCut, usbCut, ...vents, stylusBore]));
}

function plate() {
  const pbHoles = [];
  for (const sx of [-1, 1]) for (const sy of [-1, 1]) pbHoles.push([pbX + sx * P.pbHoleDx / 2, pbY + sy * P.pbHoleDy / 2]);
  const base = union([
    cube([W, D, P.plateT]),
    ...pbHoles.map(([x, y]) => cyl(P.pbStandoff, 5).translate([x, y, P.plateT])),
  ]);
  const cuts = [
    // M3 countersunk screws up into the shell's corner bosses
    ...bossPos.map(([x, y]) => union([cyl(P.plateT + 1, 3.4), cyl(1.7, 6.6, 3.4)]).translate([x, y, -0.1])),
    // M2 self-tapping pilots for the protoboard
    ...pbHoles.map(([x, y]) => cyl(P.plateT + P.pbStandoff, 1.8).translate([x, y, 1])),
  ];
  return base.subtract(union(cuts));
}

// Stand-ins for the boards, only used in the preview file.
function mockups() {
  const pcbZ = -(P.wall + standoffLen + P.pcbT);
  const cyd = onSlope(union([
    cube([P.cydW, P.cydH, P.pcbT]).translate([cx - P.cydW / 2, cy - P.cydH / 2, pcbZ]),
    cube([P.cydW - 14, P.cydH, P.dispStack]).translate([cx - P.cydW / 2 + 7, cy - P.cydH / 2, pcbZ + P.pcbT]),
    cube([60, 40, P.cydBack]).translate([cx - 30, cy - 20, pcbZ - P.cydBack]),
  ]));
  const pbZ = P.plateT + P.pbStandoff;
  const pb = union([
    cube([P.pbW, P.pbH, 1.6]).translate([pbX - P.pbW / 2, pbY - P.pbH / 2, pbZ]),
    cube([30, 3, 23]).translate([pbX - 33, pbY - 8, pbZ + 1.6]),  // receiver, upright
    cube([16, 3, 22]).translate([pbX + 12, pbY - 8, pbZ + 1.6]),  // transmitter, upright
    cyl(30, 5).translate([pbX + 20, pbY - 6.5, pbZ + 24]),        // transmitter antenna
  ]);
  const stylus = cyl(H, P.stylusD - 1).translate([styX, styY, P.plateT + 2]);
  return union([cyd.translate([0, 0, P.plateT]), pb, stylus]);
}

function writeSTL(m, path) {
  const { vertProperties: v, triVerts: tv, numProp: np } = m.getMesh();
  const n = tv.length / 3;
  const buf = Buffer.alloc(84 + 50 * n);
  buf.write('Fog Timer case', 0);
  buf.writeUInt32LE(n, 80);
  let o = 84;
  for (let i = 0; i < n; i++) {
    const p = [0, 1, 2].map((k) => [v[tv[3 * i + k] * np], v[tv[3 * i + k] * np + 1], v[tv[3 * i + k] * np + 2]]);
    const u = p[1].map((c, k) => c - p[0][k]), w = p[2].map((c, k) => c - p[0][k]);
    const nx = u[1] * w[2] - u[2] * w[1], ny = u[2] * w[0] - u[0] * w[2], nz = u[0] * w[1] - u[1] * w[0];
    const len = Math.hypot(nx, ny, nz) || 1;
    for (const c of [nx / len, ny / len, nz / len, ...p.flat()]) { buf.writeFloatLE(c, o); o += 4; }
    o += 2;
  }
  writeFileSync(new URL(path, import.meta.url), buf);
}

// Lay a part flat on the bed with its lowest corner at the origin.
function toBed(m) {
  const { min } = m.boundingBox();
  return m.translate([-min[0], -min[1], -min[2]]);
}

const s = shell(), pl = plate();
writeSTL(toBed(s.rotate([180, 0, 0])), 'fog_case_shell.stl');  // printed upside down: top on the bed
writeSTL(toBed(pl), 'fog_case_plate.stl');
writeSTL(union([s.translate([0, 0, P.plateT]), pl, mockups()]), 'fog_case_preview.stl');
// Separate pieces for viewer.html (open it through a local web server to rotate the model).
mkdirSync(new URL('preview/', import.meta.url), { recursive: true });
writeSTL(s.translate([0, 0, P.plateT]), 'preview/shell.stl');
writeSTL(pl, 'preview/plate.stl');
writeSTL(mockups(), 'preview/boards.stl');

// Fit checks: openings must be clear and the boards must not touch the case walls.
{
  const shellOnPlate = s.translate([0, 0, P.plateT]);
  const overlap = (a, b) => a.intersect(b).volume();
  const winX = cx + usbSign * (P.cydW / 2 - P.viewFromUsbEdge - P.viewW / 2);
  const winProbe = onSlope(cube([10, 10, P.wall + 2], true).translate([winX, cy, -P.wall / 2])).translate([0, 0, P.plateT]);
  const usbZ = -(P.wall + standoffLen + P.pcbT + 1.7);
  const usbProbe = onSlope(cube([P.wall + P.sideClear + 2, 4, 4]).translate([
    usbSign > 0 ? W - P.wall - P.sideClear - 1 : -1, cy - 2, usbZ - 2])).translate([0, 0, P.plateT]);
  const checks = {
    'screen window open': overlap(shell(), winProbe.translate([0, 0, -P.plateT])),
    'USB opening open': overlap(shellOnPlate, usbProbe),
    'boards clear of shell': overlap(shellOnPlate, mockups()),
    'shell sits on plate': overlap(shellOnPlate, pl),
    'stylus slides in': overlap(shellOnPlate, cyl(H + 10, P.stylusD - 0.8).translate([styX, styY, P.plateT + 0.5])),
  };
  for (const [k, v] of Object.entries(checks)) console.log(`${v < 0.01 ? 'OK  ' : 'FAIL'} ${k} (overlap ${v.toFixed(2)} mm³)`);

  // Controls: the same probes moved onto solid wall must overlap, or the checks above prove nothing.
  const controls = {
    'probe hits face beside window': overlap(shell(), onSlope(cube([10, 10, P.wall + 2], true).translate([winX, cy + P.viewH / 2 + 8, -P.wall / 2]))),
    'probe hits side wall beside USB': overlap(shellOnPlate, usbProbe.translate([0, 0, 15])),
    'stylus tube has walls': overlap(shellOnPlate, cyl(20, P.stylusD + 2.4).translate([styX, styY, P.plateT + 30])),
  };
  for (const [k, v] of Object.entries(controls)) console.log(`${v > 1 ? 'OK  ' : 'FAIL'} control: ${k} (overlap ${v.toFixed(2)} mm³)`);
}

const size = (m) => { const b = m.boundingBox(); return b.max.map((x, i) => (x - b.min[i]).toFixed(1)).join(' x '); };
console.log(`Case footprint ${W.toFixed(1)} x ${D.toFixed(1)} mm, height ${(H + P.plateT).toFixed(1)} mm`);
console.log(`Shell ${size(s)} mm, ${s.numTri()} triangles, genus ${s.genus()}`);
console.log(`Plate ${size(pl)} mm, ${pl.numTri()} triangles, genus ${pl.genus()}`);
