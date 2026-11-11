// SPDX-License-Identifier: Apache-2.0
// The garden of the demo: a plan with one area per zone. An illustration of
// the simulated installation, not of anyone's real garden. An area shows
// water when its valve really passes water in the simulation (triac on,
// relay closed, 24 VAC present, wire whole).
const NS = 'http://www.w3.org/2000/svg';

function el(name, attrs = {}, ...children) {
  const node = document.createElementNS(NS, name);
  for (const [k, v] of Object.entries(attrs)) node.setAttribute(k, String(v));
  node.append(...children);
  return node;
}

// kind: lawn (sprinklers), bed (drip line), glass (greenhouse misting).
// rects: the area; heads: sprinkler positions; label: where the badge sits.
const ZONES = [
  { kind: 'lawn', rects: [[14, 150, 180, 110, 14]], heads: [[62, 205], [146, 205]], label: [26, 164] },
  { kind: 'lawn', rects: [[234, 76, 152, 120, 14]], heads: [[274, 136], [346, 136]], label: [246, 90] },
  { kind: 'bed', rects: [[14, 76, 180, 26, 8]], plants: 'bloom', label: [26, 89] },
  { kind: 'bed', rects: [[234, 210, 44, 50, 5], [288, 210, 44, 50, 5], [342, 210, 44, 50, 5]], plants: 'plant', label: [246, 222] },
  { kind: 'bed', rects: [[14, 272, 372, 18, 9]], plants: 'plant', dense: true, label: [26, 281] },
  { kind: 'glass', rects: [[14, 10, 112, 52, 3]], plants: 'plant', label: [26, 23] },
  { kind: 'bed', rects: [[300, 10, 86, 52, 8]], plants: 'bloom', label: [312, 23] },
  { kind: 'bed', rects: [[14, 112, 180, 28, 8]], plants: 'plant', label: [26, 126] },
];

export function buildGarden(svg) {
  svg.append(
    el('rect', { class: 'path', x: 204, y: 60, width: 20, height: 240 }),
    el('rect', { class: 'path', x: 0, y: 262, width: 400, height: 8, opacity: 0 }),
    el('rect', { class: 'house', x: 150, y: -4, width: 130, height: 66, rx: 3 }),
    el('text', { x: 215, y: 30, 'text-anchor': 'middle', opacity: 0.55 }, 'House'),
  );
  const wallBox = el('rect', { class: 'wall-box', x: 160, y: 50, width: 12, height: 9, rx: 2 });
  const sensor = el('circle', { class: 'sensor', cx: 272, cy: 8, r: 4.5 });
  svg.append(wallBox, sensor);
  svg.append(el('title', {}, 'The controller is on the house wall; the rain sensor is at the roof edge.'));

  const zones = ZONES.map((z, i) => {
    const g = el('g', { class: `zone ${z.kind}`, 'data-zone': i + 1 });
    for (const [x, y, w, h, r] of z.rects) g.append(el('rect', { class: 'area', x, y, width: w, height: h, rx: r }));
    if (z.kind === 'glass') {
      const [x, y, w, h] = z.rects[0];
      for (let k = 1; k < 4; k++) {
        g.append(el('line', { x1: x + (w * k) / 4, y1: y, x2: x + (w * k) / 4, y2: y + h, stroke: 'var(--house-line)', 'stroke-width': 0.8 }));
      }
    }
    if (z.plants) {
      for (const [x, y, w, h] of z.rects) {
        const step = z.dense ? 13 : 17;
        const rows = h > 40 ? 2 : 1;
        for (let r = 0; r < rows; r++) {
          for (let px = x + 12; px < x + w - 6; px += step) {
            const py = y + (h * (r + 1)) / (rows + 1);
            // Leave the label readable.
            if (Math.abs(py - z.label[1]) < 9 && px < z.label[0] + 92) continue;
            g.append(el('circle', { class: z.plants, cx: px, cy: py, r: z.dense ? 5 : 3.4 }));
            // A dripper per plant.
            g.append(el('circle', { class: 'drip', cx: px, cy: py + 5, r: 1.5, style: `animation-delay:${((px * 7 + r * 3) % 11) / 10}s` }));
          }
        }
      }
    }
    for (const [cx, cy] of z.heads ?? []) {
      for (let k = 0; k < 3; k++) {
        g.append(el('circle', { class: 'spray', cx, cy, r: 40, style: `animation-delay:${k * 0.6}s` }));
      }
      g.append(el('circle', { cx, cy, r: 2.2, fill: 'var(--house-line)' }));
    }
    const [lx, ly] = z.label;
    const name = el('text', { class: 'name', x: lx + 12, y: ly + 3.4 });
    g.append(
      el('g', { class: 'badge' }, el('circle', { cx: lx, cy: ly, r: 8 }), el('text', { x: lx, y: ly + 3.2 }, String(i + 1))),
      name,
    );
    svg.append(g);
    return { g, name, wetUntil: 0, since: 0 };
  });

  svg.append(el('ellipse', { class: 'leak', cx: 214, cy: 128, rx: 16, ry: 9 }));
  const rain = el('g', { class: 'rain' });
  for (let k = 0; k < 46; k++) {
    const x = (k * 53) % 410;
    const y = ((k * 97) % 330) - 30;
    rain.append(el('line', { x1: x, y1: y, x2: x - 3, y2: y + 10, style: `animation-delay:${(k % 9) / 16}s` }));
  }
  svg.append(rain);

  let names = '';
  return {
    update(w, speed) {
      const now = performance.now();
      svg.classList.toggle('raining', w.rain);
      svg.classList.toggle('leaking', w.leak_lpm > 0);
      wallBox.classList.toggle('dead', !w.ac);
      const all = w.zones.map((z) => z.name + z.enabled).join('|');
      zones.forEach((z, i) => {
        const s = w.zones[i];
        z.g.classList.toggle('on', s.water);
        z.g.classList.toggle('unused', !s.enabled);
        // Driven but no water: a cut or shorted wire.
        z.g.classList.toggle('fault', s.enabled && (s.cut || s.short));
        if (s.water || w.rain) z.wetUntil = now + 90_000 / Math.min(speed, 60);
        z.g.classList.toggle('wet', now < z.wetUntil);
        if (all !== names) z.name.textContent = s.enabled ? s.name : '';
      });
      names = all;
    },
    reset() {
      zones.forEach((z) => (z.wetUntil = 0));
    },
  };
}
