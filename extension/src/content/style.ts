// Styles of the in-page UI (closed shadow root): Quiet Glass tokens from docs/DESIGN.md §2, the
// system font (a web font in every page would cost each page a download), light and dark.
export const CSS = `
:host{all:initial}
.root{
  --bg:#F3F4F8;--surface:#FFFFFF;--surface-2:#F7F8FB;--fill:#ECEEF4;--fill-press:#E2E5EE;
  --text:#0B0F19;--text-2:#454C5E;--text-3:#5E6679;--line:rgba(11,15,25,.10);--line-strong:#7C8498;
  --accent:#0B57F0;--accent-press:#0847C4;--accent-soft:#E7EEFE;--on-accent:#FFFFFF;
  --ok:#0A7A3D;--ok-soft:#E3F4EA;--warn:#8F5200;--warn-soft:#FDF0DC;--err:#C1251C;--err-soft:#FDE8E6;
  --ring-track:#DCE1EC;--led:#2F6BFF;--focus:#0B57F0;
  --sh-2:0 6px 20px rgba(11,15,25,.10),0 1px 3px rgba(11,15,25,.08),0 0 0 1px rgba(11,15,25,.06);
  --sh-3:0 18px 48px rgba(11,15,25,.18),0 2px 8px rgba(11,15,25,.08),0 0 0 1px rgba(11,15,25,.06);
  --glow:0 0 0 6px rgba(11,87,240,.14);
  --font:ui-sans-serif,system-ui,-apple-system,"Segoe UI","Noto Sans Arabic","Geeza Pro",Roboto,sans-serif;
  --mono:ui-monospace,"SF Mono",SFMono-Regular,Menlo,Consolas,"Roboto Mono",monospace;
  --ease:cubic-bezier(.2,.8,.2,1);--spring:cubic-bezier(.34,1.4,.5,1);
  position:fixed;inset:0;pointer-events:none;color-scheme:light;
  font:400 14px/1.45 var(--font);color:var(--text);-webkit-font-smoothing:antialiased;
  font-synthesis:none;text-align:start;letter-spacing:normal;text-transform:none;
}
@media (prefers-color-scheme:dark){.root{
  color-scheme:dark;
  --bg:#080A10;--surface:#12151D;--surface-2:#1B1F2A;--fill:#232837;--fill-press:#2C3243;
  --text:#F2F4F9;--text-2:#B1B8C8;--text-3:#8D95A8;--line:rgba(255,255,255,.09);--line-strong:#6B7388;
  --accent:#6C9CFF;--accent-press:#8DB1FF;--accent-soft:#202B41;--on-accent:#050A18;
  --ok:#4ADE80;--ok-soft:#14301F;--warn:#FBBF4A;--warn-soft:#33260F;--err:#FF7A70;--err-soft:#3A1815;
  --ring-track:#2A3042;--focus:#9DBBFF;
  --sh-2:0 8px 24px rgba(0,0,0,.5),0 0 0 1px rgba(255,255,255,.08);
  --sh-3:0 18px 56px rgba(0,0,0,.65),0 0 0 1px rgba(255,255,255,.09);
  --glow:0 0 0 6px rgba(108,156,255,.2);
}}
.root:lang(ar){font-size:15px;line-height:1.6}
*{box-sizing:border-box;margin:0;padding:0;font:inherit;color:inherit}
button{all:unset;box-sizing:border-box;cursor:pointer;-webkit-tap-highlight-color:transparent}
button:focus-visible,input:focus-visible{outline:2px solid var(--focus);outline-offset:2px}
.i{flex:none;display:block}
bdi{unicode-bidi:isolate}
.ltr{direction:ltr;unicode-bidi:isolate}

/* the key in the field */
.key{position:fixed;pointer-events:auto;width:22px;height:22px;border-radius:6px;display:grid;place-items:center;
  transition:transform 120ms var(--ease),box-shadow 120ms var(--ease),opacity 160ms var(--ease);
  box-shadow:0 1px 2px rgba(11,15,25,.18);animation:fade 160ms var(--ease)}
.key .logo{border-radius:6px}
.key:hover{transform:scale(1.08);box-shadow:0 2px 8px rgba(11,87,240,.35)}
.key[aria-expanded=true]{box-shadow:var(--glow)}

/* the field Keyra will type into */
.target{position:fixed;pointer-events:none;border-radius:10px;border:2px solid var(--accent);
  box-shadow:var(--glow);transition:all 200ms var(--ease);animation:fade 200ms var(--ease)}

/* dropdown */
.menu{position:fixed;pointer-events:auto;width:320px;max-width:calc(100vw - 16px);background:var(--surface);
  border-radius:16px;box-shadow:var(--sh-2);padding:6px;animation:pop 180ms var(--ease);overflow:hidden}
.menu-head{display:flex;align-items:center;gap:8px;padding:8px 10px 6px;color:var(--text-2);font-size:12.5px;font-weight:600}
.menu-head .logo{border-radius:5px}
.menu-head .host{margin-inline-start:auto;font-weight:500;color:var(--text-3);max-width:150px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.list{max-height:280px;overflow:auto;overscroll-behavior:contain}
.item{display:flex;align-items:center;gap:12px;width:100%;min-height:52px;padding:8px 10px;border-radius:12px;text-align:start}
.item:hover,.item.on{background:var(--fill)}
.item:active{background:var(--fill-press)}
.item .txt{display:flex;flex-direction:column;align-items:flex-start;min-width:0;flex:1}
.item .txt>*{max-width:100%}
.item .t1{font-weight:600;font-size:14.5px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.item .t2{font-size:12.5px;color:var(--text-2);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.item .chev{color:var(--text-3)}
.root[dir=rtl] .chev{transform:scaleX(-1)}
.item.special .t2{white-space:normal}
.item.special .badge{width:32px;height:32px;border-radius:10px;display:grid;place-items:center;background:var(--accent-soft);color:var(--accent);flex:none}
.sep{height:1px;background:var(--line);margin:4px 10px}
.empty{padding:14px 12px;color:var(--text-2);font-size:13.5px}
.foot{padding:6px 10px 8px;color:var(--text-3);font-size:12px}
.search{display:flex;align-items:center;gap:8px;margin:2px 4px 6px;padding:0 12px;height:40px;border-radius:12px;background:var(--fill);color:var(--text-3)}
.search input{all:unset;flex:1;min-width:0;height:40px;color:var(--text);font-size:14px}
.search input::placeholder{color:var(--text-3)}
.search input:focus-visible{outline:none}
.search input::-webkit-search-cancel-button{display:none}
.search:focus-within{background:var(--surface);box-shadow:inset 0 0 0 2px var(--accent)}
.mono{width:var(--s);height:var(--s);border-radius:calc(var(--s) * .3);background:var(--m);color:#fff;flex:none;
  display:grid;place-items:center;font-weight:700;font-size:calc(var(--s) * .45);line-height:1;
  background-image:linear-gradient(180deg,rgba(255,255,255,.16),rgba(255,255,255,0))}
.spin{width:18px;height:18px;border-radius:50%;border:2px solid var(--ring-track);border-top-color:var(--accent);animation:spin 700ms linear infinite;flex:none}
.loading{display:flex;align-items:center;gap:10px;padding:14px 12px;color:var(--text-2)}

/* cards (press, result, save) */
.card{position:fixed;top:16px;inset-inline-end:16px;pointer-events:auto;width:340px;max-width:calc(100vw - 24px);
  background:var(--surface);border-radius:20px;box-shadow:var(--sh-3);padding:16px;animation:drop 320ms var(--spring)}
.card-top{display:flex;gap:14px;align-items:flex-start}
.card-body{flex:1;min-width:0}
.card h2{font-size:16px;font-weight:650;line-height:1.3;margin-bottom:2px;overflow-wrap:anywhere}
.card p{font-size:13.5px;color:var(--text-2)}
.card .x{width:32px;height:32px;margin:-6px;margin-inline-start:0;border-radius:10px;display:grid;place-items:center;color:var(--text-3);flex:none}
.card .x:hover{background:var(--fill)}
.chip{display:inline-flex;align-items:center;gap:6px;margin-top:8px;padding:3px 10px;border-radius:999px;background:var(--accent-soft);color:var(--accent);font-size:12px;font-weight:600;max-width:100%}
.chip bdi{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.count{font:600 15px/1 var(--mono);color:var(--text);margin-inline-start:auto;padding-top:2px}
.count.warn{color:var(--warn)}
.hint{font-size:12px;color:var(--text-3);margin-top:10px}
.actions{display:flex;gap:8px;margin-top:14px;flex-wrap:wrap}
.btn{height:38px;padding:0 16px;border-radius:12px;display:inline-flex;align-items:center;justify-content:center;gap:6px;font-weight:600;font-size:14px;
  transition:transform 120ms var(--ease),background 120ms var(--ease)}
.btn:active{transform:scale(.97)}
.btn.primary{background:var(--accent);color:var(--on-accent);flex:1}
.btn.primary:hover{background:var(--accent-press)}
.btn.secondary{background:var(--fill);color:var(--text)}
.btn.secondary:hover{background:var(--fill-press)}
.btn.ghost{color:var(--accent);padding:0 10px}
.btn.ghost:hover{background:var(--accent-soft)}
.btn.danger{background:var(--err-soft);color:var(--err);flex:1}
.actions.stack{flex-direction:column}
.actions.stack .btn{width:100%;flex:none}
.kv{margin-top:12px;border-radius:14px;background:var(--surface-2);box-shadow:inset 0 0 0 1px var(--line)}
.kv div{display:flex;align-items:center;gap:10px;padding:9px 12px;font-size:13.5px;min-width:0}
.kv div+div{border-top:1px solid var(--line)}
.kv .k{color:var(--text-3);flex:none;display:flex}
.kv .v{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:500}
.kv .v.none{color:var(--text-3);font-weight:400}

/* the press ring (DESIGN §4.11, small) */
.ring{width:48px;height:48px;flex:none;position:relative;color:var(--accent)}
.ring svg{position:absolute;inset:0}
.ring .track{stroke:var(--ring-track)}
.ring .prog{stroke:var(--accent);stroke-dasharray:113.1;transform:rotate(-90deg);transform-origin:24px 24px;animation:drain linear forwards}
.ring .halo{stroke:var(--led);opacity:.1;transform-origin:24px 24px;animation:breathe 1600ms cubic-bezier(.4,0,.2,1) infinite}
.ring .glyph{position:absolute;inset:12px;display:grid;place-items:center}
.ring.warn .prog{stroke:var(--warn)}
.ring.warn .halo{animation:none;opacity:0}
.badge-round{width:48px;height:48px;border-radius:50%;display:grid;place-items:center;flex:none}
.tone-ok{background:var(--ok-soft);color:var(--ok)}
.tone-warn{background:var(--warn-soft);color:var(--warn)}
.tone-err{background:var(--err-soft);color:var(--err)}
.tone-info{background:var(--accent-soft);color:var(--accent)}
.tone-ok .i{animation:pop 320ms var(--spring)}
.notice{margin-top:12px;padding:10px 12px;border-radius:12px;background:var(--warn-soft);color:var(--warn);font-size:13px;display:flex;gap:8px}
.hosts{display:grid;grid-template-columns:auto 1fr;gap:4px 10px;margin-top:10px;font-size:13px}
.hosts span:nth-child(odd){color:var(--text-3)}
.hosts b{font-weight:600;overflow-wrap:anywhere}
.sr{position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0 0 0 0);white-space:nowrap}

@keyframes fade{from{opacity:0}}
@keyframes pop{from{opacity:0;transform:scale(.96)}}
@keyframes drop{from{opacity:0;transform:translateY(-12px) scale(.98)}}
@keyframes spin{to{transform:rotate(360deg)}}
@keyframes drain{from{stroke-dashoffset:0}to{stroke-dashoffset:113.1}}
@keyframes breathe{0%,100%{opacity:.10;transform:scale(1)}50%{opacity:.45;transform:scale(1.12)}}
@media (prefers-reduced-motion:reduce){*{animation-duration:1ms!important;animation-iteration-count:1!important;transition:none!important}
  .ring .prog{animation-duration:var(--d)!important}}
`;
