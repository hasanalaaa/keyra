// UI state machines for the two kinds of "press Keyra's button" waits (DESIGN §4.11):
// type actions (state.pending / state.last) and presence ops (state.presence).
import { useEffect, useRef, useState } from 'preact/hooks';
import { ApiError, api, isAwaiting, type Awaiting } from './api';
import { errorText, isLockedError } from './errors';
import { getState, holdFastPolling, loadEntries, toast, trackRequest, useApp } from './store';
import { osOf, resolveTarget, wantsSwitch } from './hostos';
import { t } from './i18n';
import type { DeviceState, Presence, PresenceOp, RequestStatus, ResultCode, TypeTextRequest, TypeWhat } from './types';

export type ErrorCode = Exclude<ResultCode, 'typed' | 'cancelled'>;

export type Phase =
  | { kind: 'idle' }
  | { kind: 'ready'; deadline: number; total: number }
  | { kind: 'typing' }
  | { kind: 'typed' }
  | { kind: 'error'; code: ErrorCode };

/** Ticks every `ms` while `active`, returning Date.now(). */
export function useNow(active: boolean, ms = 1000): number {
  const [now, setNow] = useState(Date.now());
  useEffect(() => {
    if (!active) return;
    setNow(Date.now());
    const h = setInterval(() => setNow(Date.now()), ms);
    return () => clearInterval(h);
  }, [active, ms]);
  return now;
}

/** SPEC §12.6a: the device's answer about our own request, when this poll asked about it. */
export function ownRequest(d: DeviceState | null | undefined, requestId: string | null | undefined): RequestStatus | null {
  return requestId && d?.request?.requestId === requestId ? d.request : null;
}

/** How our own type action ended, or null while it has not. 'superseded': a newer one of ours replaced it. */
export function typeOutcome(r: RequestStatus | null): ResultCode | 'superseded' | null {
  if (!r || r.kind === 'presence' || !['typed', 'cancelled', 'expired', 'failed'].includes(r.state)) return null;
  if (r.code === 'superseded') return 'superseded';
  if (r.state === 'failed') return r.code && r.code !== 'done' ? r.code : 'failed';
  return r.state as ResultCode;
}

/** How our own presence op ended, or null while it has not. */
export function presenceOutcome(r: RequestStatus | null): 'done' | 'failed' | 'expired' | 'cancelled' | null {
  if (!r || r.kind === 'type') return null;
  return r.state === 'done' || r.state === 'failed' || r.state === 'expired' || r.state === 'cancelled' ? r.state : null;
}

const TYPED_DWELL = 1900;
const RESULT_FRESH_MS = 6000;

type What = TypeWhat | 'test' | 'text' | 'probe';

interface Act {
  what: What;
  startedAt: number;
  deadline: number;
  total: number;
  goneAt: number;
  requestId?: string; // SPEC §12.6a; absent when re-attached to an action armed before a reload
}

/** Type action for one entry, or with id 0 the settings type test / free text / Layout Doctor probe (`free`). */
export function useTypeAction(id: number, free: 'test' | 'text' | 'probe' = 'test') {
  const app = useApp();
  const [act, setAct] = useState<Act | null>(null);
  const [outcome, setOutcome] = useState<Phase | null>(null);
  const actRef = useRef(act);
  actRef.current = act;

  const matches = (d: DeviceState, a: Act) =>
    d.pending !== null && d.pending.what === a.what && (id === 0 || d.pending.id === id);

  // Re-attach to a pending action for this entry that was started earlier (sheet reopened, page reloaded).
  useEffect(() => {
    const p = app.device?.pending;
    if (!p || actRef.current || (id !== 0 && p.id !== id) || (id === 0 && p.what !== free)) return;
    const total = Math.max(60000, p.expiresIn);
    setAct({ what: p.what, startedAt: Date.now() - (total - p.expiresIn), deadline: Date.now() + p.expiresIn, total, goneAt: 0 });
  }, [app.device?.pending, id]);

  useEffect(() => {
    if (!act) return;
    return holdFastPolling();
  }, [act !== null]);

  useEffect(() => (act?.requestId ? trackRequest(act.requestId) : undefined), [act?.requestId]);

  const ended = (code: ResultCode) => {
    setAct(null);
    if (code === 'cancelled') toast(t('cancelled'));
    else if (code === 'typed') {
      setOutcome({ kind: 'typed' });
      navigator.vibrate?.(12);
      void loadEntries();
    } else {
      setOutcome({ kind: 'error', code });
      if (code === 'entry_changed') void loadEntries(); // show the edited account before a retry
    }
  };

  useEffect(() => {
    const d = app.device;
    const a = act;
    if (!d || !a || app.deviceAt < a.startedAt) return;
    // The device names our own request: its outcome, never another one's (SPEC §12.6a).
    const own = typeOutcome(ownRequest(d, a.requestId));
    if (own === 'superseded') return setAct(null); // replaced by a newer action of ours
    if (own) return ended(own);
    if (matches(d, a)) {
      const deadline = Date.now() + d.pending!.expiresIn;
      if (Math.abs(deadline - a.deadline) > 400 || a.goneAt) setAct({ ...a, deadline, goneAt: 0 });
      return;
    }
    const last = d.last;
    const elapsed = Date.now() - a.startedAt;
    if (last && last.at <= RESULT_FRESH_MS && last.at < elapsed && (!last.what || last.what === a.what)) {
      ended(last.code);
      return;
    }
    if (d.pending) {
      setAct(null); // replaced by an action started elsewhere
      return;
    }
    if (!a.goneAt) setAct({ ...a, goneAt: Date.now() });
    else if (Date.now() - a.goneAt > 10000) setAct(null); // never heard back; return to the buttons
  }, [app.device]);

  // Typed ✓ dwells, then the card collapses back to the buttons.
  useEffect(() => {
    if (outcome?.kind !== 'typed') return;
    const h = setTimeout(() => setOutcome(null), TYPED_DWELL);
    return () => clearTimeout(h);
  }, [outcome]);

  const lastWhat = useRef<What | null>(null);
  const start = async (what: What, target?: string, text?: TypeTextRequest): Promise<boolean> => {
    lastWhat.current = what;
    setOutcome(null);
    try {
      const startedAt = Date.now();
      const s = getState();
      const resolved = resolveTarget(target, s.device?.host.output ?? null, s.ble);
      const sw = wantsSwitch(resolved, osOf(resolved, s.device?.host.usbOs, s.ble));
      const r =
        what === 'test'
          ? await api.typeTest(target, sw)
          : what === 'probe'
            ? await api.typeProbe(target, sw)
            : what === 'text'
              ? await api.typeText({ ...text!, target, switchLang: sw })
              : await api.type(id, what, target, sw);
      const total = Math.max(1000, r.pending.expiresIn);
      setAct({ what, startedAt, deadline: Date.now() + r.pending.expiresIn, total, goneAt: 0, requestId: r.requestId });
      return true;
    } catch (e) {
      if (!isLockedError(e)) toast(errorText(e), 'error');
      return false;
    }
  };

  const cancel = async () => {
    try {
      await api.cancelType(actRef.current?.requestId);
      setAct(null);
      toast(t('cancelled'));
    } catch (e) {
      // 409 not_cancelled / superseded: nothing of this session waits (another session's, already
      // done, or replaced by a newer one of ours); otherwise still armed on Keyra. Either way the
      // card keeps following the device.
      const gone = e instanceof ApiError && (e.code === 'not_cancelled' || e.code === 'superseded');
      toast(t(gone ? 'nothingWaiting' : 'cancelFailed'), 'error');
    }
  };

  let phase: Phase = { kind: 'idle' };
  if (outcome) phase = outcome;
  else if (act) phase = act.goneAt ? { kind: 'typing' } : { kind: 'ready', deadline: act.deadline, total: act.total };

  // After a result the action is gone, but "Try again" / "Copy instead" still
  // need to know which field it was.
  return { phase, what: act?.what ?? lastWhat.current, start, cancel, dismiss: () => setOutcome(null) };
}

export type PresencePhase =
  | { kind: 'idle' }
  | { kind: 'ready'; deadline: number; total: number }
  | { kind: 'done' }
  | { kind: 'failed' }
  | { kind: 'expired' }
  | { kind: 'cancelled' };

/**
 * Presence ops (setup, Wi-Fi change, restore-replace, factory reset). The outcome comes from
 * `state.presence.result` for this op, newer than our request. `doneOnDisconnect`: the op restarts
 * the access point right after it commits, so losing the link before we read the result means it worked.
 */
// Cancel tokens live in sessionStorage: this tab (and only it) can still
// withdraw its op after a reload.
const CANCEL_KEY = 'keyra.cancel.';
function cancelToken(op: PresenceOp): string | null {
  try {
    return sessionStorage.getItem(CANCEL_KEY + op);
  } catch {
    return null;
  }
}
function keepCancelToken(op: PresenceOp, token: string): void {
  try {
    sessionStorage.setItem(CANCEL_KEY + op, token);
  } catch {
    // Private mode: only this page load can cancel.
  }
}
function forgetCancelToken(op: PresenceOp): void {
  try {
    sessionStorage.removeItem(CANCEL_KEY + op);
  } catch {
    // Nothing stored.
  }
}

/**
 * True when the device (polled at `polledAt`) reports an outcome for `op` that came after
 * `armedAt` and nothing for `op` is waiting: the one slot that held it is gone.
 * `polledAt` is when the poll was sent, so the check errs towards "not settled".
 */
export function presenceSettled(p: Presence | undefined, polledAt: number, op: PresenceOp, armedAt: number, own: RequestStatus | null = null): boolean {
  // Asked by its requestId (SPEC §12.6a): the device says exactly whether it still waits.
  if (own && polledAt >= armedAt) {
    if (presenceOutcome(own)) return true;
    if (own.state === 'armed' || own.state === 'waiting') return false;
  }
  if (!p || polledAt < armedAt) return false;
  if (p.awaiting && p.op === op) return false;
  return p.result?.op === op && polledAt - p.result.at > armedAt;
}

export function usePresence(op: PresenceOp, opts: { doneOnDisconnect?: boolean } = {}) {
  const app = useApp();
  const [st, setSt] = useState<{ startedAt: number; deadline: number; total: number; seen: boolean; requestId?: string } | null>(null);
  const [phase, setPhase] = useState<PresencePhase>({ kind: 'idle' });
  // True while the device holds this op for us: leaving must withdraw it, or a
  // later press would still run it (a cancelled factory reset used to erase).
  const active = useRef(false);
  const token = useRef<string | null>(null); // also in memory: storage may be blocked
  const requestId = useRef<string | undefined>(undefined); // SPEC §12.6a
  const armedAt = useRef(0); // when the request that armed it was sent
  const withdraw = () => {
    if (!active.current) return;
    active.current = false;
    const t = token.current ?? cancelToken(op);
    forgetCancelToken(op);
    // Already pressed (e.g. a factory reset ends the session, and this sheet unmounts before
    // seeing the result): nothing is left to withdraw.
    const { device, deviceAt } = getState();
    if (presenceSettled(device?.presence, deviceAt, op, armedAt.current, ownRequest(device, requestId.current))) return;
    if (t) void api.cancelPresence(op, t, requestId.current).catch(() => undefined); // expires on its own after 60 s anyway
  };

  useEffect(() => {
    if (!st) return;
    return holdFastPolling();
  }, [st !== null]);

  useEffect(() => (st?.requestId ? trackRequest(st.requestId) : undefined), [st?.requestId]);

  useEffect(() => withdraw, []); // unmounted while waiting

  const finish = (p: PresencePhase) => {
    active.current = false;
    forgetCancelToken(op);
    setSt(null);
    setPhase(p);
  };

  useEffect(() => {
    if (!st) return;
    if (!app.online) {
      if (opts.doneOnDisconnect && st.seen) finish({ kind: 'done' });
      return;
    }
    const d = app.device;
    if (!d || app.deviceAt < st.startedAt) return;
    // The device names our own op: its outcome, never another one's (SPEC §12.6a).
    const own = presenceOutcome(ownRequest(d, st.requestId));
    if (own) return finish({ kind: own });
    const r = d.presence.result;
    if (r && r.op === op && r.at < Date.now() - st.startedAt) return finish({ kind: r.code });
    if (d.presence.awaiting && d.presence.op === op) {
      const deadline = Date.now() + d.presence.expiresIn;
      if (!st.seen || Math.abs(deadline - st.deadline) > 400) {
        setSt({ ...st, deadline, seen: true });
        setPhase({ kind: 'ready', deadline, total: st.total });
      }
    } else if (Date.now() > st.deadline + 5000) finish({ kind: 'expired' }); // result never arrived
  }, [app.device, app.online]);

  const begin = (startedAt: number, r: Awaiting) => {
    active.current = true;
    armedAt.current = startedAt;
    token.current = r.cancel ?? null;
    requestId.current = r.requestId;
    if (r.cancel) keepCancelToken(op, r.cancel);
    const deadline = Date.now() + r.expiresIn;
    setSt({ startedAt, deadline, total: r.expiresIn, seen: false, requestId: r.requestId });
    setPhase({ kind: 'ready', deadline, total: r.expiresIn });
  };

  /** Sends the request that opens the op; false (with a toast) when the device refused it. */
  const start = async (req: () => Promise<Awaiting>): Promise<boolean> => {
    try {
      const startedAt = Date.now();
      let r: Awaiting;
      try {
        r = await req();
      } catch (e) {
        // Our own op from before a reload still waits on the device: withdraw it
        // (this tab kept its cancel token) and ask again.
        const stale = cancelToken(op);
        if (!(e instanceof ApiError && e.code === 'busy' && stale && getState().device?.presence.op === op)) throw e;
        await api.cancelPresence(op, stale);
        forgetCancelToken(op);
        r = await req();
      }
      begin(startedAt, r);
      return true;
    } catch (e) {
      if (!isLockedError(e)) toast(errorText(e), 'error');
      return false;
    }
  };

  /** Watches an op that a request already opened (e.g. PUT /settings or /restore returned 202). */
  const watch = (sentAt: number, r: Awaiting) => begin(sentAt, r);

  const abandon = () => {
    withdraw();
    finish({ kind: 'idle' });
  };

  return { phase, start, watch, abandon };
}

/**
 * Requests that need a press of Keyra's button first (SPEC §12.3: reveal, backup, recovery key).
 * `run(req)` sends the request; on 202 it waits for the press, then sends it once more (the press
 * opened this session's grace) and resolves with that answer. null = cancelled, expired or refused.
 */
export function usePressGate(op: PresenceOp) {
  const presence = usePresence(op);
  const waiting = useRef<{ retry: () => Promise<unknown>; resolve: (v: unknown) => void } | null>(null);

  useEffect(() => {
    const w = waiting.current;
    const k = presence.phase.kind;
    if (!w || k === 'idle' || k === 'ready') return;
    waiting.current = null;
    presence.abandon();
    if (k !== 'done') {
      if (k === 'failed') toast(t('genericError'), 'error');
      w.resolve(null);
      return;
    }
    w.retry().then(
      (r) => w.resolve(isAwaiting(r) ? null : r),
      (e) => {
        if (!isLockedError(e)) toast(errorText(e), 'error');
        w.resolve(null);
      },
    );
  }, [presence.phase.kind]);

  // A gate left waiting (sheet closed) resolves to null so callers never hang.
  useEffect(() => () => waiting.current?.resolve(null), []);

  async function run<T>(req: () => Promise<T | Awaiting>): Promise<T | null> {
    const sent = Date.now();
    const r = await req();
    if (!isAwaiting(r)) return r;
    return new Promise<T | null>((resolve) => {
      waiting.current?.resolve(null);
      waiting.current = { retry: req, resolve: resolve as (v: unknown) => void };
      presence.watch(sent, r);
    });
  }

  const cancel = () => {
    const w = waiting.current;
    waiting.current = null;
    presence.abandon();
    w?.resolve(null);
  };

  return { phase: presence.phase, run, cancel };
}
