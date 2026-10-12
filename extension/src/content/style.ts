// Styles of the in-page UI (closed shadow root): Graphite & Ivory from docs/brand/BRAND.md §3, the
// system font (a web font in every page would cost each page a download), light and dark. Monochrome;
// signal blue appears only while Keyra waits for its button (the Contour, the outlined field).
export const CSS = `
:host{all:initial}
.root{
  --bg:#F4F3EF;--surface:#FFFFFF;--surface-2:#EFEEE9;--fill:#E9E8E3;--fill-press:#DFDED8;
  --text:#111214;--text-2:#4A4C52;--text-3:#66686F;--line:rgba(17,18,20,.10);--line-2:rgba(17,18,20,.16);
  --on-ink:#FFFFFF;--signal:#1F5BEF;--ok:#0A7A3D;--warn:#8F5200;--err:#C1251C;--focus:#111214;
  --sh-2:0 0 0 1px var(--line),0 1px 2px rgba(17,18,20,.04),0 8px 24px -6px rgba(17,18,20,.14);
  --sh-3:0 0 0 1px var(--line),0 2px 4px rgba(17,18,20,.04),0 20px 48px -12px rgba(17,18,20,.24);
  --font:ui-sans-serif,system-ui,-apple-system,"Segoe UI","Noto Sans Arabic","Geeza Pro",Roboto,sans-serif;
  --mono:ui-monospace,"SF Mono",SFMono-Regular,Menlo,Consolas,"Roboto Mono",monospace;
  --ease:cubic-bezier(.2,.8,.2,1);
  --spring:linear(0,.009,.035 2.1%,.141 4.4%,.723 12.9%,.938 16.7%,1.017 20%,1.067,1.077 25.4%,1.05 29.4%,1.004 36.3%,.989 41.7%,1.001 58.3%,1);
  position:fixed;inset:0;pointer-events:none;color-scheme:light;
  font:400 14px/1.45 var(--font);color:var(--text);-webkit-font-smoothing:antialiased;
  font-synthesis:none;text-align:start;letter-spacing:normal;text-transform:none;
}
@media (prefers-color-scheme:dark){.root{
  color-scheme:dark;
  --bg:#0B0C0E;--surface:#141518;--surface-2:#1B1C20;--fill:#222328;--fill-press:#2B2C32;
  --text:#F2F2EF;--text-2:#A9ABB2;--text-3:#8A8C93;--line:rgba(255,255,255,.08);--line-2:rgba(255,255,255,.14);
  --on-ink:#0B0C0E;--signal:#6C9CFF;--ok:#4ADE80;--warn:#FBBF4A;--err:#FF7A70;--focus:#F2F2EF;
  --sh-2:0 0 0 1px var(--line),0 8px 28px -4px rgba(0,0,0,.6);
  --sh-3:0 0 0 1px var(--line),0 24px 56px -8px rgba(0,0,0,.7);
}}
.root:lang(ar){font-size:15px;line-height:1.6}
*{box-sizing:border-box;margin:0;padding:0;font:inherit;color:inherit}
button{all:unset;box-sizing:border-box;cursor:pointer;-webkit-tap-highlight-color:transparent}
button:focus-visible,input:focus-visible{outline:2px solid var(--focus);outline-offset:2px}
.i,.logo{flex:none;display:block}
bdi{unicode-bidi:isolate}
.ltr{direction:ltr;unicode-bidi:isolate}

/* the key in the field */
.key{position:fixed;pointer-events:auto;width:22px;height:22px;border-radius:5px;display:grid;place-items:center;
  transition:transform 120ms var(--ease),box-shadow 120ms var(--ease),opacity 160ms var(--ease);
  box-shadow:0 1px 2px rgba(0,0,0,.22);animation:fade 160ms var(--ease)}
.key:hover{transform:scale(1.08);box-shadow:0 2px 8px rgba(0,0,0,.28)}
.key[aria-expanded=true]{box-shadow:0 0 0 2px var(--surface),0 0 0 3.5px var(--text)}

/* the field Keyra will type into: waiting for the press, so signal blue */
.target{position:fixed;pointer-events:none;border-radius:10px;border:2px solid var(--signal);
  box-shadow:0 0 0 4px color-mix(in srgb,var(--signal) 16%,transparent);transition:all 200ms var(--ease);animation:fade 200ms var(--ease)}

/* dropdown */
.menu{position:fixed;pointer-events:auto;width:320px;max-width:calc(100vw - 16px);background:var(--surface);
  border-radius:14px;box-shadow:var(--sh-2);padding:6px;animation:pop 200ms var(--ease);overflow:hidden}
.menu-head{display:flex;align-items:center;gap:8px;min-height:36px;padding:4px 10px 6px;font-size:13px;font-weight:600;letter-spacing:-.005em;
  border-bottom:1px solid var(--line);margin:0 -6px 4px;padding-inline:16px 14px}
.menu-head .logo{color:var(--text)}
.menu-head .host{margin-inline-start:auto;font:400 11.5px/1 var(--mono);color:var(--text-3);max-width:160px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.menu-head .x{margin-inline-start:-6px}
.list{max-height:280px;overflow:auto;overscroll-behavior:contain}
.item{display:flex;align-items:center;gap:12px;width:100%;min-height:50px;padding:8px 10px;border-radius:10px;text-align:start;transition:background 120ms var(--ease)}
.item:hover,.item.on{background:var(--fill)}
.item:active{background:var(--fill-press)}
.item:focus-visible{outline-offset:-2px}
.item .txt{display:flex;flex-direction:column;align-items:flex-start;min-width:0;flex:1}
.item .txt>*{max-width:100%}
.item .t1{font-weight:550;font-size:14px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.item .t2{font-size:12.5px;color:var(--text-3);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.item .chev{color:var(--text-3)}
.root[dir=rtl] .chev{transform:scaleX(-1)}
.item.special .t2{white-space:normal}
.item.special .badge{width:32px;height:32px;border-radius:9px;display:grid;place-items:center;background:var(--surface-2);box-shadow:inset 0 0 0 1px var(--line);color:var(--text);flex:none}
.sep{height:1px;background:var(--line);margin:4px 4px}
.empty{padding:12px 10px;color:var(--text-2);font-size:13.5px}
.foot{padding:6px 10px 6px;color:var(--text-3);font-size:12px}
.search{display:flex;align-items:center;gap:8px;margin:2px 4px 6px;padding:0 12px;height:38px;border-radius:10px;background:var(--fill);color:var(--text-3);transition:box-shadow 120ms var(--ease),background 120ms var(--ease)}
.search input{all:unset;flex:1;min-width:0;height:38px;color:var(--text);font-size:14px}
.search input::placeholder{color:var(--text-3)}
.search input:focus-visible{outline:none}
.search input::-webkit-search-cancel-button{display:none}
.search:focus-within{background:var(--surface);box-shadow:inset 0 0 0 1.5px var(--focus)}
.mono{width:var(--s);height:var(--s);border-radius:calc(var(--s) * .28);background:var(--surface-2);box-shadow:inset 0 0 0 1px var(--line);color:var(--text);flex:none;
  display:grid;place-items:center;font-weight:600;font-size:calc(var(--s) * .44);line-height:1}
.spin{width:18px;height:18px;border-radius:50%;border:2px solid var(--line-2);border-top-color:var(--text-2);animation:spin 700ms linear infinite;flex:none}
.loading{display:flex;align-items:center;gap:10px;padding:12px 10px;color:var(--text-2)}

/* cards (press, result, save) */
.card{position:fixed;top:16px;inset-inline-end:16px;pointer-events:auto;width:344px;max-width:calc(100vw - 24px);
  background:var(--surface);border-radius:16px;box-shadow:var(--sh-3);padding:18px;animation:drop 320ms var(--ease)}
.card-top{display:flex;gap:14px;align-items:flex-start}
.card-body{flex:1;min-width:0;padding-top:1px}
.card h2{font-size:15px;font-weight:600;line-height:1.35;letter-spacing:-.01em;margin-bottom:3px;overflow-wrap:anywhere}
.root:lang(ar) .card h2{letter-spacing:0;font-size:16px}
.card p{font-size:13.5px;color:var(--text-2)}
.card .x{width:28px;height:28px;margin:-4px;margin-inline-start:0;border-radius:8px;display:grid;place-items:center;color:var(--text-3);flex:none;transition:background 120ms var(--ease),color 120ms var(--ease)}
.card .x:hover,.menu .x:hover{background:var(--fill);color:var(--text)}
.chip{display:inline-flex;align-items:center;gap:6px;padding:3px 8px;border-radius:6px;background:var(--fill);color:var(--text-2);font-size:12px;font-weight:500;max-width:100%;min-width:0}
.chip bdi{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.meta{display:flex;align-items:center;gap:8px;margin-top:14px;min-width:0}
.count{font:500 13px/1 var(--mono);font-variant-numeric:tabular-nums;color:var(--text-2);margin-inline-start:auto;flex:none}
.count.warn{color:var(--warn)}
.hint{font-size:12px;color:var(--text-3);margin-top:12px}
.actions{display:flex;gap:8px;margin-top:16px;flex-wrap:wrap}
.btn{height:36px;padding:0 14px;border-radius:10px;display:inline-flex;align-items:center;justify-content:center;gap:6px;font-weight:550;font-size:13.5px;
  transition:transform 120ms var(--ease),background 120ms var(--ease),opacity 120ms var(--ease)}
.btn:active{transform:scale(.98)}
.btn.primary{background:var(--text);color:var(--on-ink);flex:1}
.btn.primary:hover{opacity:.88}
.btn.secondary{background:var(--fill);color:var(--text)}
.btn.secondary:hover{background:var(--fill-press)}
.btn.ghost{color:var(--text-2);padding:0 10px;flex:1 0 100%;height:32px;margin-top:-2px}
.btn.ghost:hover{background:var(--fill);color:var(--text)}
.btn.danger{background:var(--err);color:#fff;flex:1}
.btn.danger:hover{opacity:.9}
.actions.stack{flex-direction:column}
.actions.stack .btn{width:100%;flex:none}
.kv{margin-top:14px;border-radius:10px;background:var(--surface-2);box-shadow:inset 0 0 0 1px var(--line)}
.kv div{display:flex;align-items:center;gap:10px;padding:9px 12px;font-size:13.5px;min-width:0}
.kv div+div{border-top:1px solid var(--line)}
.kv .k{color:var(--text-3);flex:none;display:flex}
.kv .v{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:500}
.kv .v.none{color:var(--text-3);font-weight:400}

/* the press indicator: the Contour (BRAND §5) */
.ring{width:40px;height:40px;flex:none;display:grid;place-items:center}
.contour{display:block;overflow:visible}
.contour path{fill:none;stroke:currentColor;stroke-width:7;stroke-linecap:round;stroke-linejoin:round;stroke-dasharray:92.8 100}
.contour.wait{color:var(--signal)}
.contour.wait path{animation:gap 1200ms cubic-bezier(.45,0,.55,1) infinite}
.contour.done{color:var(--text)}
.contour.done path{animation:close 420ms var(--spring) 80ms both}
.drain{height:2px;margin-top:12px;border-radius:2px;background:var(--line);overflow:hidden}
.drain i{display:block;height:100%;background:var(--text-3);transform-origin:left;animation:drain linear forwards}
.root[dir=rtl] .drain i{transform-origin:right}
.drain.warn i{background:var(--warn)}
.badge-round{width:40px;height:40px;border-radius:50%;display:grid;place-items:center;flex:none;background:color-mix(in srgb,currentColor 11%,transparent)}
.badge-tile{flex:none;display:block}
.tone-ok{color:var(--ok)}
.tone-warn{color:var(--warn)}
.tone-err{color:var(--err)}
.tone-info{color:var(--text-2)}
.badge-round.tone-info{background:var(--fill)}
.notice{margin-top:12px;padding:10px 12px;border-radius:10px;background:color-mix(in srgb,var(--warn) 10%,var(--surface));color:var(--warn);font-size:13px;display:flex;gap:8px}
.notice .i{margin-top:2px}
.hosts{display:grid;grid-template-columns:auto 1fr;gap:6px 12px;margin-top:12px;padding:10px 12px;border-radius:10px;background:var(--surface-2);box-shadow:inset 0 0 0 1px var(--line);font-size:13px}
.hosts span:nth-child(odd){color:var(--text-3)}
.hosts b{font-weight:500;font-family:var(--mono);font-size:12.5px;overflow-wrap:anywhere}
.sr{position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0 0 0 0);white-space:nowrap}

@keyframes fade{from{opacity:0}}
@keyframes pop{from{opacity:0;transform:translateY(-4px) scale(.98)}}
@keyframes drop{from{opacity:0;transform:translateY(-8px)}}
@keyframes spin{to{transform:rotate(360deg)}}
@keyframes drain{from{transform:scaleX(1)}to{transform:scaleX(0)}}
/* pathLength 100, gap 7.2: breathe it open by 4 at each end, the LED's 1.2 s; close it on success. */
@keyframes gap{50%{stroke-dasharray:84.8 100;stroke-dashoffset:-4}}
@keyframes close{from{stroke-dasharray:92.8 100}to{stroke-dasharray:100 100}}
@media (prefers-reduced-motion:reduce){*{animation-duration:1ms!important;animation-iteration-count:1!important;animation-delay:0s!important;transition:none!important}
  .contour.wait path{animation:none!important}
  .card,.menu{animation:fade 120ms linear!important}
  .drain i{animation-duration:var(--d)!important;animation-delay:var(--at)!important}}
`;
