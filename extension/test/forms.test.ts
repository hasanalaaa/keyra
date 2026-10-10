// @vitest-environment happy-dom
import { beforeEach, describe, expect, it } from 'vitest';
import { findForms, roleOf, snapshot, whatFor, type LoginForm } from '../src/forms';

// happy-dom has no layout, so visibility comes from a data attribute in these fixtures.
const visible = (el: HTMLElement) => !el.closest('[data-hidden]');
const load = (html: string): LoginForm[] => {
  document.body.innerHTML = html;
  return findForms(document, visible);
};
const $ = (sel: string) => document.querySelector(sel) as HTMLInputElement;

beforeEach(() => {
  document.title = '';
});

describe('sign-in forms', () => {
  it('GitHub-style form: username then current password', () => {
    const [f, ...rest] = load(`
      <form action="/session">
        <label for="login_field">Username or email address</label><input type="text" name="login" id="login_field" autocomplete="username">
        <label for="password">Password</label><input type="password" name="password" id="password" autocomplete="current-password">
        <input type="submit" value="Sign in">
      </form>`);
    expect(rest).toHaveLength(0);
    expect(f.kind).toBe('login');
    expect(f.username).toBe($('#login_field'));
    expect(f.current).toBe($('#password'));
    expect(f.newPassword).toBeUndefined();
    expect(whatFor(f, 'username')).toEqual({ what: 'both', focus: $('#login_field') });
    expect(whatFor(f, 'current')).toEqual({ what: 'password', focus: $('#password') });
  });

  it('without autocomplete, names and labels decide (Arabic)', () => {
    const [f] = load(`
      <form>
        <label>الاسم الكامل <input name="q" type="search"></label>
        <label>البريد الإلكتروني أو رقم الهاتف <input id="u" type="text" name="field1"></label>
        <label>كلمة المرور <input id="p" type="password" name="field2"></label>
        <button>تسجيل الدخول</button>
      </form>`);
    expect(f.kind).toBe('login');
    expect(f.username).toBe($('#u'));
    expect(f.current).toBe($('#p'));
  });

  it('a page without a <form> still groups its fields', () => {
    const forms = load(`
      <header><input type="search" name="q" placeholder="Search"></header>
      <main><div class="card"><div><input id="u" type="email" placeholder="Email"></div><div><input id="p" type="password" placeholder="Password"></div><div role="button">Log in</div></div></main>`);
    expect(forms).toHaveLength(1);
    expect(forms[0].username).toBe($('#u'));
    expect(forms[0].current).toBe($('#p'));
  });

  it('ignores hidden, disabled and one-time-code fields', () => {
    const forms = load(`
      <form data-hidden><input type="text" name="username"><input type="password" name="password"></form>
      <form><input type="password" name="otp" autocomplete="one-time-code"><input type="password" disabled></form>`);
    expect(forms).toHaveLength(0);
  });

  it('prefers the field marked as username over a nearer email field', () => {
    const [f] = load(`
      <form><input id="u" name="user_id" type="text"><input id="e" name="contact" type="text"><input id="p" type="password"></form>`);
    expect(f.username).toBe($('#u'));
  });

  it('a password field the site switched to text (show password) stays a password field', () => {
    load(`<form><input id="u" autocomplete="username"><input id="p" type="password"></form>`);
    $('#p').type = 'text';
    const [f] = findForms(document, visible);
    expect(f.current).toBe($('#p'));
  });

  it('the first step that asks only for the username', () => {
    const [f] = load(`
      <form><h1>Sign in</h1><input id="u" type="email" name="identifier" autocomplete="username"><button>Next</button></form>`);
    expect(f.kind).toBe('username');
    expect(whatFor(f, 'username')).toEqual({ what: 'username', focus: $('#u') });
  });

  it('a newsletter box is not a username step', () => {
    expect(load(`<form><h2>Newsletter</h2><input type="email" name="email"><button>Subscribe</button></form>`)).toHaveLength(0);
  });
});

describe('new passwords', () => {
  it('sign-up with new-password and confirmation', () => {
    const [f] = load(`
      <form id="signup">
        <input id="n" name="name" autocomplete="name">
        <input id="u" type="email" name="email" autocomplete="email">
        <input id="p1" type="password" autocomplete="new-password">
        <input id="p2" type="password" autocomplete="new-password">
        <button>Create account</button>
      </form>`);
    expect(f.kind).toBe('signup');
    expect(f.username).toBe($('#u'));
    expect(f.newPassword).toBe($('#p1'));
    expect(f.confirm).toBe($('#p2'));
    expect(f.current).toBeUndefined();
    expect(roleOf([f], $('#p2'))?.role).toBe('confirm');
    expect(whatFor(f, 'username')?.what).toBe('username');
  });

  it('sign-up found from names alone', () => {
    const [f] = load(`
      <form action="/users/register">
        <input id="u" name="username"><input id="p1" type="password" name="password"><input id="p2" type="password" name="password_confirmation">
        <button type="submit">Register</button>
      </form>`);
    expect(f.kind).toBe('signup');
    expect(f.newPassword).toBe($('#p1'));
    expect(f.confirm).toBe($('#p2'));
  });

  it('one password field under a "Create account" button is a new password', () => {
    const [f] = load(`<form><input id="u" type="email" name="email"><input id="p" type="password" name="password"><button>Create account</button></form>`);
    expect(f.kind).toBe('signup');
    expect(f.newPassword).toBe($('#p'));
  });

  it('change password: current, new, confirm', () => {
    const [f] = load(`
      <form action="/settings/password">
        <input type="text" name="username" autocomplete="username" value="hasanalaaa" data-hidden>
        <label>Old password <input id="o" type="password"></label>
        <label>New password <input id="n" type="password"></label>
        <label>Confirm new password <input id="c" type="password"></label>
        <button>Update password</button>
      </form>`);
    expect(f.kind).toBe('change');
    expect(f.current).toBe($('#o'));
    expect(f.newPassword).toBe($('#n'));
    expect(f.confirm).toBe($('#c'));
    // The invisible username the page keeps for password managers names the login to update.
    expect(f.username).toBeUndefined();
    $('#n').value = 'new-one';
    expect(snapshot(f)).toEqual({ username: 'hasanalaaa', password: 'new-one', isNew: true });
  });

  it('three unlabeled password fields are current, new, confirm by position', () => {
    const [f] = load(`<form><input id="a" type="password"><input id="b" type="password"><input id="c" type="password"></form>`);
    expect([f.current, f.newPassword, f.confirm]).toEqual([$('#a'), $('#b'), $('#c')]);
    expect(f.kind).toBe('change');
  });

  it('Arabic change-password labels', () => {
    const [f] = load(`
      <form><label>كلمة المرور الحالية<input id="o" type="password"></label><label>كلمة المرور الجديدة<input id="n" type="password"></label><label>تأكيد كلمة المرور<input id="c" type="password"></label></form>`);
    expect(f.current).toBe($('#o'));
    expect(f.newPassword).toBe($('#n'));
    expect(f.confirm).toBe($('#c'));
  });

  it('two forms on one page stay apart', () => {
    const forms = load(`
      <form id="in"><input id="u1" name="login"><input id="p1" type="password" autocomplete="current-password"></form>
      <form id="up"><input id="u2" type="email" name="email"><input id="p2" type="password" autocomplete="new-password"><input id="p3" type="password" autocomplete="new-password"></form>`);
    expect(forms.map((f) => f.kind)).toEqual(['login', 'signup']);
    expect(roleOf(forms, $('#p2'))?.form.root.id).toBe('up');
  });
});

describe('save snapshot', () => {
  it('takes the new password when there is one', () => {
    const [f] = load(`<form><input id="u" name="email" type="email" value=" a@b.c "><input id="o" type="password" autocomplete="current-password" value="old"><input id="n" type="password" autocomplete="new-password" value="new-one"></form>`);
    expect(snapshot(f)).toEqual({ username: 'a@b.c', password: 'new-one', isNew: true });
  });

  it('a sign-in gives the current password, and nothing without one', () => {
    const [f] = load(`<form><input id="u" name="login" value="me"><input id="p" type="password" value=""></form>`);
    expect(snapshot(f)).toBeNull();
    $('#p').value = 'secret';
    expect(snapshot(f)).toEqual({ username: 'me', password: 'secret', isNew: false });
  });
});
