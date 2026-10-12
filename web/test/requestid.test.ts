import { describe, expect, it } from 'vitest';
import { ownRequest, presenceOutcome, presenceSettled, typeOutcome } from '../src/lib/actions';
import type { DeviceState, Presence, RequestStatus } from '../src/lib/types';

const RID = '0123456789abcdef0123456789abcdef';
const state = (request?: RequestStatus) => ({ request }) as DeviceState;

// SPEC §12.6a: each screen follows its own request by requestId, never another one's outcome.
describe('own request', () => {
  it('is the answer about our requestId only', () => {
    expect(ownRequest(state({ requestId: RID, state: 'armed' }), RID)?.state).toBe('armed');
    expect(ownRequest(state({ requestId: 'f'.repeat(32), state: 'armed' }), RID)).toBeNull();
    expect(ownRequest(state(), RID)).toBeNull();
    expect(ownRequest(state({ requestId: RID, state: 'armed' }), undefined)).toBeNull(); // an older firmware: no id
    expect(ownRequest(null, RID)).toBeNull();
  });

  it('turns a finished type action into its result code', () => {
    const r = (s: RequestStatus['state'], code?: RequestStatus['code']): RequestStatus => ({ requestId: RID, kind: 'type', state: s, code });
    expect(typeOutcome(r('armed'))).toBeNull();
    expect(typeOutcome(r('waiting'))).toBeNull();
    expect(typeOutcome(r('typed', 'typed'))).toBe('typed');
    expect(typeOutcome(r('cancelled', 'cancelled'))).toBe('cancelled');
    expect(typeOutcome(r('cancelled', 'superseded'))).toBe('superseded');
    expect(typeOutcome(r('expired', 'expired'))).toBe('expired');
    expect(typeOutcome(r('failed', 'entry_changed'))).toBe('entry_changed');
    expect(typeOutcome(r('failed'))).toBe('failed');
    expect(typeOutcome({ requestId: RID, kind: 'presence', state: 'done' })).toBeNull();
    expect(typeOutcome(null)).toBeNull();
  });

  it('turns a finished presence op into its outcome', () => {
    expect(presenceOutcome({ requestId: RID, kind: 'presence', state: 'done', code: 'done' })).toBe('done');
    expect(presenceOutcome({ requestId: RID, kind: 'presence', state: 'cancelled', code: 'superseded' })).toBe('cancelled');
    expect(presenceOutcome({ requestId: RID, kind: 'presence', state: 'armed' })).toBeNull();
    expect(presenceOutcome({ requestId: RID, state: 'none' })).toBeNull();
    expect(presenceOutcome({ requestId: RID, kind: 'type', state: 'typed' })).toBeNull();
  });

  it('decides whether leaving must still withdraw the op', () => {
    const idle: Presence = { awaiting: false, op: null, expiresIn: 0, result: null };
    const busyOther: Presence = { awaiting: true, op: 'backup', expiresIn: 5000, result: null };
    // Finished by its own id: nothing to withdraw, whatever else the slot shows.
    expect(presenceSettled(busyOther, 12_000, 'backup', 10_000, { requestId: RID, kind: 'presence', state: 'done' })).toBe(true);
    // Still waiting by its own id: withdraw it.
    expect(presenceSettled(idle, 12_000, 'backup', 10_000, { requestId: RID, kind: 'presence', state: 'armed' })).toBe(false);
    // Without an id (older firmware): the earlier rule.
    expect(presenceSettled(busyOther, 12_000, 'backup', 10_000)).toBe(false);
  });
});
