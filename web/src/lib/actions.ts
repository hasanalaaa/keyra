// UI state machines for the two kinds of "press Keyra's button" waits (DESIGN §4.11):
// type actions (state.pending / state.last) and presence ops (state.presence).
import { useEffect, useRef, useState } from 'preact/hooks';
import { api, isAwaiting, type Awaiting } from './api';
import { errorText, isLockedError } from './errors';
import { holdFastPolling, loadEntries, toast, useApp } from './store';
import { t } from './i18n';
import type { DeviceState, PresenceOp, ResultCode, TypeTextRequest, TypeWhat } from './types';

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

const TYPED_DWELL = 1900;
const RESULT_FRESH_MS = 6000;

type What = TypeWhat | 'test' | 'text';

interface Act {
  what: What;
  startedAt: number;
  deadline: number;
  total: number;
  goneAt: number;
}

/** Type action for one entry, or with id 0 the settings type test / free text (`free`). */
export function useTypeAction(id: number, free: 'test' | 'text' = 'test') {
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

  useEffect(() => {
    const d = app.device;
    const a = act;
    if (!d || !a || app.deviceAt < a.startedAt) return;
    if (matches(d, a)) {
      const deadline = Date.now() + d.pending!.expiresIn;
      if (Math.abs(deadline - a.deadline) > 400 || a.goneAt) setAct({ ...a, deadline, goneAt: 0 });
      return;
    }
    const last = d.last;
    const elapsed = Date.now() - a.startedAt;
    if (last && last.at <= RESULT_FRESH_MS && last.at < elapsed && (!last.what || last.what === a.what)) {
      setAct(null);
      if (last.code === 'cancelled') toast(t('cancelled'));
      else if (last.code === 'typed') {
        setOutcome({ kind: 'typed' });
        navigator.vibrate?.(12);
        void loadEntries();
      } else setOutcome({ kind: 'error', code: last.code });
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

  const start = async (what: What, target?: string, text?: TypeTextRequest): Promise<boolean> => {
    setOutcome(null);
    try {
      const startedAt = Date.now();
      const r =
        what === 'test' ? await api.typeTest(target) : what === 'text' ? await api.typeText({ ...text!, target }) : await api.type(id, what, target);
      const total = Math.max(1000, r.pending.expiresIn);
      setAct({ what, startedAt, deadline: Date.now() + r.pending.expiresIn, total, goneAt: 0 });
      return true;
    } catch (e) {
      if (!isLockedError(e)) toast(errorText(e), 'error');
      return false;
    }
  };

  const cancel = async () => {
    setAct(null);
    try {
      await api.cancelType();
      toast(t('cancelled'));
    } catch {
      // The device expires it on its own after 60 s.
    }
  };

  let phase: Phase = { kind: 'idle' };
  if (outcome) phase = outcome;
  else if (act) phase = act.goneAt ? { kind: 'typing' } : { kind: 'ready', deadline: act.deadline, total: act.total };

  return { phase, what: act?.what ?? null, start, cancel, dismiss: () => setOutcome(null) };
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
export function usePresence(op: PresenceOp, opts: { doneOnDisconnect?: boolean } = {}) {
  const app = useApp();
  const [st, setSt] = useState<{ startedAt: number; deadline: number; total: number; seen: boolean } | null>(null);
  const [phase, setPhase] = useState<PresencePhase>({ kind: 'idle' });

  useEffect(() => {
    if (!st) return;
    return holdFastPolling();
  }, [st !== null]);

  const finish = (p: PresencePhase) => {
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
    const deadline = Date.now() + r.expiresIn;
    setSt({ startedAt, deadline, total: r.expiresIn, seen: false });
    setPhase({ kind: 'ready', deadline, total: r.expiresIn });
  };

  /** Sends the request that opens the op; false (with a toast) when the device refused it. */
  const start = async (req: () => Promise<Awaiting>): Promise<boolean> => {
    try {
      const startedAt = Date.now();
      begin(startedAt, await req());
      return true;
    } catch (e) {
      if (!isLockedError(e)) toast(errorText(e), 'error');
      return false;
    }
  };

  /** Watches an op that a request already opened (e.g. PUT /settings or /restore returned 202). */
  const watch = (sentAt: number, r: Awaiting) => begin(sentAt, r);

  const abandon = () => finish({ kind: 'idle' });

  return { phase, start, watch, abandon };
}

/**
 * Requests that need a press of Keyra's button first (SPEC §10.3: reveal, backup, recovery key).
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
