// Demo websites for the e2e run, served over HTTPS from one local server. Chromium maps the
// hosts to 127.0.0.1 (--host-resolver-rules), so the extension sees real host names.
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync } from 'node:fs';
import { createServer } from 'node:https';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const SITE_NAME = { 'github.com': 'GitHub', 'newsite.example': 'New Site' };

const page = (host, title, body) => `<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
${SITE_NAME[host] ? `<meta property="og:site_name" content="${SITE_NAME[host]}">` : ''}
<title>${title}</title>
<style>
  :root{color-scheme:light dark;font:15px/1.5 ui-sans-serif,system-ui,-apple-system,"Segoe UI",sans-serif}
  body{margin:0;min-height:100vh;display:grid;place-items:center;background:light-dark(#f6f7f9,#0d1117);color:light-dark(#1f2328,#e6edf3)}
  .demo-box{width:340px;padding:28px;border-radius:12px;background:light-dark(#fff,#161b22);border:1px solid light-dark(#d0d7de,#30363d)}
  .demo-box h1{font-size:22px;font-weight:600;margin:0 0 4px;text-align:center}
  .demo-box p{margin:0 0 20px;text-align:center;color:light-dark(#59636e,#9198a1);font-size:13px}
  .demo-box label{display:block;font-size:14px;font-weight:500;margin:14px 0 6px}
  .demo-box input{box-sizing:border-box;width:100%;height:36px;padding:0 12px;border-radius:6px;border:1px solid light-dark(#d0d7de,#3d444d);background:light-dark(#fff,#0d1117);color:inherit;font:inherit}
  .demo-box button{margin-top:20px;width:100%;height:36px;border:0;border-radius:6px;background:#1f883d;color:#fff;font:600 14px/1 inherit;cursor:pointer}
  .demo-badge{position:fixed;top:12px;left:12px;font:600 11px/1 ui-monospace,monospace;padding:6px 8px;border-radius:6px;background:light-dark(#eaeef2,#21262d);color:light-dark(#59636e,#9198a1)}
</style></head><body><span class="demo-badge">${host} · demo page</span>${body}</body></html>`;

const login = (host) =>
  page(host, 'Sign in', `<form class="demo-box" method="post" action="/session">
  <h1>Sign in</h1><p>to ${host}</p>
  <label for="login_field">Username or email address</label>
  <input type="text" name="login" id="login_field" autocomplete="username" autocapitalize="off">
  <label for="password">Password</label>
  <input type="password" name="password" id="password" autocomplete="current-password">
  <button type="submit">Sign in</button>
</form>`);

const signup = (host) =>
  page(host, 'Create your account', `<form class="demo-box" method="post" action="/signup">
  <h1>Create your account</h1><p>${host}</p>
  <label for="email">Email</label><input type="email" name="email" id="email" autocomplete="email">
  <label for="new">Password</label><input type="password" name="password" id="new" autocomplete="new-password">
  <label for="confirm">Confirm password</label><input type="password" name="password_confirmation" id="confirm" autocomplete="new-password">
  <button type="submit">Create account</button>
</form>`);

const change = (host) =>
  page(host, 'Change password', `<form class="demo-box" method="post" action="/settings/password">
  <h1>Change password</h1><p>${host}</p>
  <input type="text" name="username" autocomplete="username" value="hasanalaaa" style="display:none">
  <label for="old">Old password</label><input type="password" id="old" name="old" autocomplete="current-password">
  <label for="new">New password</label><input type="password" id="new" name="new" autocomplete="new-password">
  <label for="confirm">Confirm new password</label><input type="password" id="confirm" name="confirm" autocomplete="new-password">
  <button type="submit">Update password</button>
</form>`);

const done = (host, text) => page(host, text, `<div class="demo-box"><h1>${text}</h1><p>${host}</p></div>`);

export async function startSite() {
  const dir = mkdtempSync(join(tmpdir(), 'keyra-e2e-cert-'));
  execFileSync('openssl', ['req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '2', '-subj', '/CN=keyra-e2e', '-keyout', join(dir, 'k.pem'), '-out', join(dir, 'c.pem')], { stdio: 'ignore' });
  const server = createServer({ key: readFileSync(join(dir, 'k.pem')), cert: readFileSync(join(dir, 'c.pem')) }, (req, res) => {
    const host = (req.headers.host ?? '').replace(/:\d+$/, '');
    const path = new URL(req.url, 'https://x').pathname;
    const html = (s) => {
      res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' });
      res.end(s);
    };
    const go = (to) => {
      req.resume();
      req.on('end', () => {
        res.writeHead(303, { Location: to });
        res.end();
      });
    };
    if (req.method === 'POST') {
      if (path === '/session') return go('/home');
      if (path === '/signup') return go('/welcome');
      if (path === '/settings/password') return go('/settings/saved');
    }
    if (path === '/login') return html(login(host));
    if (path === '/signup') return html(signup(host));
    if (path === '/settings/password') return html(change(host));
    if (path === '/home') return html(done(host, 'Signed in'));
    if (path === '/welcome') return html(done(host, 'Welcome!'));
    if (path === '/settings/saved') return html(done(host, 'Password changed'));
    res.writeHead(404).end('not found');
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  return { port: server.address().port, stop: () => server.close() };
}
