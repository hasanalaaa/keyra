// Finds sign-in, sign-up and change-password forms in a page (SPEC §9.4 "In the page").
// Signals in order of trust: autocomplete tokens, input type, then names/labels/placeholders
// in English and Arabic. Pure DOM reading — nothing here changes the page.

export type FormKind = 'login' | 'signup' | 'change' | 'username';
export type FieldRole = 'username' | 'current' | 'new' | 'confirm';

export interface LoginForm {
  kind: FormKind;
  /** The <form>, or the nearest container that holds the fields when the page uses none. */
  root: Element;
  username?: HTMLInputElement;
  current?: HTMLInputElement;
  newPassword?: HTMLInputElement;
  confirm?: HTMLInputElement;
}

export type Visible = (el: HTMLElement) => boolean;

export function isVisible(el: HTMLElement): boolean {
  if (!el.isConnected) return false;
  const r = el.getBoundingClientRect();
  if (r.width < 4 || r.height < 4) return false;
  const check = (el as HTMLElement & { checkVisibility?: (o: object) => boolean }).checkVisibility;
  return check ? check.call(el, { checkOpacity: true, checkVisibilityCSS: true }) : getComputedStyle(el).visibility !== 'hidden';
}

const TEXTUAL = new Set(['text', 'email', 'tel', '']);
const NOT_LOGIN = /search|query|captcha|coupon|promo|otp|totp|2fa|mfa|one.?time|verif\w*.?code|security.?code|sms.?code|cvv|cvc|card|zip|postal|city|street|address|amount|quantity|message|comment|subject|title|url|website|company|first.?name|last.?name|full.?name|given|family|birth|age\b/i;
const USERNAME_HINT = /user|login|account|identifier|email|e-mail|mail|phone|mobile|اسم المستخدم|المستخدم|البريد|الإيميل|الهاتف|الجوال|رقم الموبايل|الحساب/i;
const STRONG_USERNAME = /^(user(name|_?id)?|login(_?id|name)?|email|e-mail|identifier|account|uid|loginfmt|session\[username_or_email\])$/i;
const OTP_FIELD = /otp|totp|2fa|mfa|one.?time|verif\w*.?code|security.?code|sms.?code|cvv|cvc|card.?(code|number)|رمز التحقق/i;
const CURRENT_HINT = /\bold|current|existing|previous|الحالية|الحالي|القديمة|القديم/i;
const CONFIRM_HINT = /confirm|repeat|again|retype|re-?enter|re-?type|verify|second|password2|pass2|pwd2|تأكيد|أعد|اعد|مرة أخرى/i;
const NEW_HINT = /\bnew|create|choose|set.?password|newpass|جديدة|جديد/i;
const SIGNUP_HINT = /sign.?up|register|registration|create.?(an.?|your.?)?account|join|new.?account|get.?started|إنشاء حساب|انشاء حساب|تسجيل جديد|حساب جديد|اشترك|اشتراك/i;
const CHANGE_HINT = /change.?password|update.?password|reset.?password|new.?password|تغيير كلمة|تعيين كلمة|إعادة تعيين/i;
const LOGIN_HINT = /log.?in|sign.?in|signin|next|continue|تسجيل الدخول|دخول|التالي|متابعة/i;

/** Text a page gives an input: name, id, placeholder, aria-label and its label(s). */
export function fieldText(el: HTMLInputElement): string {
  const parts = [el.name, el.id, el.placeholder, el.getAttribute('aria-label') ?? '', el.getAttribute('data-testid') ?? ''];
  for (const l of Array.from(el.labels ?? [])) parts.push(l.textContent ?? '');
  const by = el.getAttribute('aria-labelledby');
  if (by) for (const id of by.split(/\s+/)) parts.push(el.ownerDocument.getElementById(id)?.textContent ?? '');
  return parts.join(' ').replace(/\s+/g, ' ').trim();
}

const autocompleteOf = (el: HTMLInputElement) => (el.getAttribute('autocomplete') ?? '').toLowerCase().split(/\s+/);

// Password fields stay password fields after a site's "show password" switch turns them to text.
const seenPassword = new WeakSet<HTMLInputElement>();

export function isPasswordField(el: HTMLInputElement): boolean {
  const ac = autocompleteOf(el);
  if (ac.includes('one-time-code')) return false;
  const pw = el.type === 'password' || (seenPassword.has(el) && el.type === 'text') || ac.includes('current-password') || ac.includes('new-password');
  if (!pw) return false;
  if (OTP_FIELD.test(`${el.name} ${el.id}`)) return false;
  seenPassword.add(el);
  return true;
}

function usable(el: HTMLInputElement, visible: Visible): boolean {
  return !el.disabled && !el.readOnly && el.type !== 'hidden' && visible(el);
}

function isUsernameCandidate(el: HTMLInputElement): boolean {
  if (!TEXTUAL.has(el.type) || isPasswordField(el)) return false;
  const ac = autocompleteOf(el);
  if (ac.includes('username') || ac.includes('email') || ac.includes('webauthn')) return true;
  if (ac.includes('one-time-code') || ac.some((t) => /^(cc-|address|postal|street|given|family|name$|organization|tel-extension)/.test(t))) return false;
  if (el.getAttribute('role') === 'combobox' && !USERNAME_HINT.test(fieldText(el))) return false;
  return !NOT_LOGIN.test(`${el.name} ${el.id} ${el.placeholder}`) || el.type === 'email';
}

function usernameScore(el: HTMLInputElement): number {
  const ac = autocompleteOf(el);
  if (ac.includes('username') || ac.includes('webauthn')) return 4;
  if (STRONG_USERNAME.test(el.name) || STRONG_USERNAME.test(el.id)) return 3;
  if (ac.includes('email') || el.type === 'email') return 2;
  return USERNAME_HINT.test(fieldText(el)) ? 1 : 0;
}

const inputsIn = (root: Element) => Array.from(root.querySelectorAll('input')) as HTMLInputElement[];

/** The group of fields a password field belongs to: its form, or the smallest ancestor that has a username-like input. */
function groupRoot(pw: HTMLInputElement, visible: Visible): Element {
  if (pw.form) return pw.form;
  let el: Element = pw;
  let best: Element = pw.parentElement ?? pw;
  for (let i = 0; i < 8 && el.parentElement && el.parentElement !== el.ownerDocument.documentElement; i++) {
    el = el.parentElement;
    best = el;
    if (inputsIn(el).some((x) => x !== pw && usable(x, visible) && (isUsernameCandidate(x) || isPasswordField(x)))) break;
  }
  return best;
}

/** Words that describe the whole form: its attributes, buttons and headings. */
function formText(root: Element): string {
  const parts = [root.id, root.getAttribute('name') ?? '', root.getAttribute('action') ?? '', root.getAttribute('class') ?? '', root.getAttribute('aria-label') ?? ''];
  for (const b of Array.from(root.querySelectorAll('button, input[type=submit], [role=button], h1, h2, h3, legend'))) {
    parts.push((b as HTMLInputElement).value && b.tagName === 'INPUT' ? (b as HTMLInputElement).value : (b.textContent ?? ''));
  }
  const doc = root.ownerDocument;
  if (root === doc.body || root.tagName === 'FORM') parts.push(doc.title);
  return parts.join(' ').replace(/\s+/g, ' ').slice(0, 2000);
}

function precedes(a: Element, b: Element): boolean {
  return !!(a.compareDocumentPosition(b) & Node.DOCUMENT_POSITION_FOLLOWING);
}

function classify(root: Element, pws: HTMLInputElement[], visible: Visible): LoginForm {
  const roles = new Map<HTMLInputElement, FieldRole>();
  // 1. What the page says outright.
  for (const p of pws) {
    const ac = autocompleteOf(p);
    if (ac.includes('current-password')) roles.set(p, 'current');
    else if (ac.includes('new-password')) roles.set(p, [...roles.values()].includes('new') ? 'confirm' : 'new');
  }
  // 2. Names and labels.
  for (const p of pws) {
    if (roles.has(p)) continue;
    const text = fieldText(p);
    if (CURRENT_HINT.test(text)) roles.set(p, 'current');
    else if (CONFIRM_HINT.test(text)) roles.set(p, 'confirm');
    else if (NEW_HINT.test(text)) roles.set(p, [...roles.values()].includes('new') ? 'confirm' : 'new');
  }
  // 3. Position and count.
  const text = formText(root);
  const left = pws.filter((p) => !roles.has(p));
  const taken = () => new Set(roles.values());
  if (pws.length === 1 && left.length === 1) {
    roles.set(left[0], SIGNUP_HINT.test(text) && !LOGIN_HINT.test(text.replace(SIGNUP_HINT, '')) ? 'new' : 'current');
  } else {
    for (const p of left) {
      const t = taken();
      if (pws.length >= 3 && !t.has('current')) roles.set(p, 'current');
      else if (!t.has('new')) roles.set(p, 'new');
      else if (!t.has('confirm')) roles.set(p, 'confirm');
      else roles.set(p, 'confirm');
    }
  }
  // A lone "confirm" without a "new" is the new one.
  if (!taken().has('new')) {
    const c = pws.find((p) => roles.get(p) === 'confirm');
    if (c) roles.set(c, 'new');
  }

  const form: LoginForm = { kind: 'login', root };
  for (const p of pws) {
    const r = roles.get(p)!;
    if (r === 'current' && !form.current) form.current = p;
    else if (r === 'new' && !form.newPassword) form.newPassword = p;
    else if (r === 'confirm' && !form.confirm) form.confirm = p;
  }
  form.kind = form.newPassword ? (form.current || CHANGE_HINT.test(text) ? 'change' : 'signup') : 'login';

  // The username: the best-scored candidate before the first password field, nearest wins ties.
  const first = pws[0];
  const before = inputsIn(root).filter((x) => usable(x, visible) && isUsernameCandidate(x) && precedes(x, first));
  let pick: HTMLInputElement | undefined;
  let best = -1;
  for (const c of before) {
    const s = usernameScore(c);
    if (s >= best) {
      best = s;
      pick = c;
    }
  }
  if (pick) form.username = pick;
  return form;
}

/** A first step that asks only for the username (Google, Microsoft, banks). */
function usernameOnly(root: Element, visible: Visible): HTMLInputElement | undefined {
  const fields = inputsIn(root).filter((x) => usable(x, visible) && TEXTUAL.has(x.type));
  if (fields.length === 0 || fields.length > 2) return undefined;
  const cand = fields.filter((x) => isUsernameCandidate(x) && usernameScore(x) >= 2);
  if (cand.length !== 1) return undefined;
  const ac = autocompleteOf(cand[0]);
  if (ac.includes('username') || ac.includes('webauthn')) return cand[0];
  const text = formText(root);
  return LOGIN_HINT.test(text) && !SIGNUP_HINT.test(text) ? cand[0] : undefined;
}

/** Every login-related form in the document, in page order. */
export function findForms(doc: Document, visible: Visible = isVisible): LoginForm[] {
  const all = inputsIn(doc.documentElement);
  const pws = all.filter((x) => isPasswordField(x) && usable(x, visible));
  const groups = new Map<Element, HTMLInputElement[]>();
  for (const p of pws) {
    const root = groupRoot(p, visible);
    groups.set(root, [...(groups.get(root) ?? []), p]);
  }
  const out: LoginForm[] = [];
  for (const [root, list] of groups) out.push(classify(root, list, visible));
  // Forms without a password field that ask for the username first.
  for (const f of Array.from(doc.forms)) {
    if (groups.has(f) || inputsIn(f).some((x) => isPasswordField(x))) continue;
    const u = usernameOnly(f, visible);
    if (u) out.push({ kind: 'username', root: f, username: u });
  }
  if (out.length === 0 && doc.forms.length === 0) {
    const u = usernameOnly(doc.body ?? doc.documentElement, visible);
    if (u) out.push({ kind: 'username', root: doc.body ?? doc.documentElement, username: u });
  }
  return out;
}

/** The form and role of one input, if it is a field Keyra helps with. */
export function roleOf(forms: LoginForm[], el: Element): { form: LoginForm; role: FieldRole } | null {
  for (const form of forms) {
    if (form.username === el) return { form, role: 'username' };
    if (form.current === el) return { form, role: 'current' };
    if (form.newPassword === el) return { form, role: 'new' };
    if (form.confirm === el) return { form, role: 'confirm' };
  }
  return null;
}

/** What Keyra should type from the field the user opened it on (SPEC §9.4 "Fill by typing"). */
export function whatFor(form: LoginForm, role: FieldRole): { what: 'username' | 'password' | 'both'; focus: HTMLInputElement } | null {
  if (role === 'username' && form.username) {
    return form.current && form.kind === 'login' ? { what: 'both', focus: form.username } : { what: 'username', focus: form.username };
  }
  if (role === 'current' && form.current) return { what: 'password', focus: form.current };
  if ((role === 'new' || role === 'confirm') && form.newPassword) return { what: 'password', focus: role === 'confirm' && form.confirm ? form.confirm : form.newPassword };
  return null;
}

/** The values to offer for saving once the form is sent, or null when there is no password. */
export function snapshot(form: LoginForm): { username: string; password: string; isNew: boolean } | null {
  const fresh = form.newPassword?.value ?? '';
  const password = fresh || form.current?.value || '';
  if (!password) return null;
  return { username: form.username?.value.trim() ?? '', password, isNew: !!fresh };
}
