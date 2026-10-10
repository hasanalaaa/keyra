// Tiny DOM helpers shared by the page UI and the popup. Text always goes in as text nodes (titles
// come from the vault and pages), and icons are built element by element, so no HTML is parsed —
// which also keeps pages with Trusted Types or a strict CSP working.

type Child = Node | string | null | undefined | false;
type Attrs = Record<string, string | number | boolean | null | undefined | ((e: Event) => void)>;

export function h<K extends keyof HTMLElementTagNameMap>(tag: K, attrs: Attrs = {}, ...children: Child[]): HTMLElementTagNameMap[K] {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v === null || v === undefined || v === false) continue;
    if (typeof v === 'function') el.addEventListener(k.replace(/^on/, ''), v as EventListener);
    else if (k === 'class') el.className = String(v);
    else el.setAttribute(k, v === true ? '' : String(v));
  }
  for (const c of children) if (c !== null && c !== undefined && c !== false) el.append(c);
  return el;
}

const NS = 'http://www.w3.org/2000/svg';

// Lucide icons (ISC licence, lucide.dev) as DESIGN §4.0 uses them: 24 grid, stroke 1.75, round.
// "c:cx,cy,r" is a circle, "r:x,y,w,h,rx" a rectangle, anything else a path.
const ICONS = {
  search: ['c:11,11,8', 'm21 21-4.3-4.3'],
  sparkles: ['M9.937 15.5A2 2 0 0 0 8.5 14.063l-6.135-1.582a.5.5 0 0 1 0-.962L8.5 9.936A2 2 0 0 0 9.937 8.5l1.582-6.135a.5.5 0 0 1 .963 0L14.063 8.5A2 2 0 0 0 15.5 9.937l6.135 1.581a.5.5 0 0 1 0 .964L15.5 14.063a2 2 0 0 0-1.437 1.437l-1.582 6.135a.5.5 0 0 1-.963 0z', 'M20 3v4', 'M22 5h-4', 'M4 17v2', 'M5 18H3'],
  x: ['M18 6 6 18', 'm6 6 12 12'],
  check: ['M20 6 9 17l-5-5'],
  alert: ['m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3', 'M12 9v4', 'M12 17h.01'],
  clock: ['c:12,12,10', 'M12 6v6l4 2'],
  lock: ['r:3,11,18,11,2', 'M7 11V7a5 5 0 0 1 10 0v4'],
  offline: ['M12 20h.01', 'M8.5 16.429a5 5 0 0 1 7 0', 'M5 12.859a10 10 0 0 1 5.17-2.69', 'M19 12.859a10 10 0 0 0-2.007-1.523', 'M2 8.82a15 15 0 0 1 4.177-2.643', 'M22 8.82a15 15 0 0 0-11.288-3.764', 'm2 2 20 20'],
  back: ['m15 18-6-6 6-6'],
  next: ['m9 18 6-6-6-6'],
  keyboard: ['r:2,4,20,16,2', 'M6 8h.01', 'M10 8h.01', 'M14 8h.01', 'M18 8h.01', 'M8 12h.01', 'M12 12h.01', 'M16 12h.01', 'M7 16h10'],
  copy: ['r:8,8,14,14,2', 'M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2'],
  refresh: ['M3 12a9 9 0 0 1 9-9 9.75 9.75 0 0 1 6.74 2.74L21 8', 'M21 3v5h-5', 'M21 12a9 9 0 0 1-9 9 9.75 9.75 0 0 1-6.74-2.74L3 16', 'M8 16H3v5'],
  save: ['M12 15V3', 'M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4', 'm7 10 5 5 5-5'],
  shield: ['M20 13c0 5-3.5 7.5-7.66 8.95a1 1 0 0 1-.67-.01C7.5 20.5 4 18 4 13V6a1 1 0 0 1 1-1c2 0 4.5-1.2 6.24-2.72a1.17 1.17 0 0 1 1.52 0C14.51 3.81 17 5 19 5a1 1 0 0 1 1 1z', 'm9 12 2 2 4-4'],
  open: ['M15 3h6v6', 'M10 14 21 3', 'M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6'],
  trash: ['M3 6h18', 'M19 6v14c0 1-1 2-2 2H7c-1 0-2-1-2-2V6', 'M8 6V4c0-1 1-2 2-2h4c1 0 2 1 2 2'],
  user: ['M19 21v-2a4 4 0 0 0-4-4H9a4 4 0 0 0-4 4v2', 'c:12,7,4'],
  globe: ['c:12,12,10', 'M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20', 'M2 12h20'],
  usb: ['c:10,7,1', 'c:4,20,1', 'M4.7 19.3 19 5', 'm21 3-3 1 2 2Z', 'M9.26 7.68 5 12l2 5', 'm10 14 5 2 3.5-3.5', 'm18 12 1-1 1 1-1 1Z'],
  plug: ['M12 22v-5', 'M9 8V2', 'M15 8V2', 'M18 8v5a4 4 0 0 1-4 4h-4a4 4 0 0 1-4-4V8Z'],
} as const;
export type IconName = keyof typeof ICONS;

function shape(spec: string): SVGElement {
  const [kind, rest] = spec.includes(':') && /^[cr]:/.test(spec) ? spec.split(':') : ['p', spec];
  if (kind === 'c') {
    const [cx, cy, r] = rest.split(',');
    return svgEl('circle', { cx, cy, r });
  }
  if (kind === 'r') {
    const [x, y, width, height, rx] = rest.split(',');
    return svgEl('rect', { x, y, width, height, rx });
  }
  return svgEl('path', { d: rest });
}

export function svgEl(tag: string, attrs: Record<string, string | number>): SVGElement {
  const el = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, String(v));
  return el;
}

export function icon(name: IconName, size = 20, cls = ''): SVGElement {
  const svg = svgEl('svg', { viewBox: '0 0 24 24', width: size, height: size, fill: 'none', stroke: 'currentColor', 'stroke-width': 1.75, 'stroke-linecap': 'round', 'stroke-linejoin': 'round', 'aria-hidden': 'true', class: `i ${cls}`.trim() });
  for (const s of ICONS[name]) svg.append(shape(s));
  return svg;
}

/** Keyra's mark (DESIGN §1.3): a key whose bow is a ring. `tile` adds the blue rounded square. */
export function logo(size: number, tile = true): SVGElement {
  const svg = svgEl('svg', { viewBox: '0 0 64 64', width: size, height: size, 'aria-hidden': 'true', class: 'logo' });
  if (tile) svg.append(svgEl('rect', { width: 64, height: 64, rx: 16, fill: '#0B57F0' }));
  const g = svgEl('g', { fill: 'none', stroke: tile ? '#fff' : 'currentColor', 'stroke-width': 6, 'stroke-linecap': 'round', 'stroke-linejoin': 'round' });
  g.append(svgEl('circle', { cx: 32, cy: 22, r: 9 }), svgEl('path', { d: 'M32 31v19M32 41h9M32 50h6' }));
  svg.append(g);
  return svg;
}

// Monogram (DESIGN §4.3): deterministic colour from the title, never a network icon.
const PALETTE = ['#C2410C', '#B45309', '#4D7C0F', '#15803D', '#0F766E', '#0E7490', '#1D4ED8', '#6D28D9', '#A21CAF', '#BE185D', '#B91C1C', '#475569'];

function fnv1a(s: string): number {
  let x = 0x811c9dc5;
  for (const ch of s) {
    x ^= ch.codePointAt(0)!;
    x = Math.imul(x, 0x01000193) >>> 0;
  }
  return x >>> 0;
}

export function monogramLetter(title: string): string {
  const t = title.trim();
  const chars = Array.from(t.startsWith('ال') && t.length > 2 ? t.slice(2) : t);
  const c = chars.find((ch) => /[\p{L}\p{N}]/u.test(ch)) ?? '';
  return c.toLocaleUpperCase('en');
}

export function monogram(title: string, size = 32): HTMLElement {
  const letter = monogramLetter(title);
  const el = h('span', { class: 'mono', 'aria-hidden': 'true', style: `--m:${PALETTE[fnv1a(title.trim().toLowerCase().normalize('NFKC')) % 12]};--s:${size}px` });
  if (letter) el.textContent = letter;
  else el.append(logo(Math.round(size * 0.6), false));
  return el;
}
