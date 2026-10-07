import { describe, expect, it } from 'vitest';
import { issueCount } from '../src/screens/Health';

describe('issueCount', () => {
  it('counts each flagged account once', () => {
    expect(issueCount({ checked: 9, clock: true, weak: [{ id: 1, level: 1 }], reused: [[1, 2], [3, 4, 5]], old: [{ id: 2, since: 1 }, { id: 6, since: 1 }] })).toBe(6);
    expect(issueCount({ checked: 3, clock: false, weak: [], reused: [], old: [] })).toBe(0);
  });
});
