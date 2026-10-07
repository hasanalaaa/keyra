import { afterEach, describe, expect, it, vi } from 'vitest';
import { api } from '../src/lib/api';

// The firmware matches bond addresses with their colons (it also decodes %3A,
// but older builds did not): the client must send them as they are.
describe('api paths', () => {
  afterEach(() => vi.unstubAllGlobals());
  it('sends Bluetooth bond addresses unencoded', async () => {
    const urls: string[] = [];
    vi.stubGlobal('fetch', async (url: string) => {
      urls.push(url);
      return new Response(null, { status: 204 });
    });
    await api.bleSetOs('A4:C1:38:0B:7F:3A', 'mac');
    await api.bleForget('A4:C1:38:0B:7F:3A');
    expect(urls).toEqual(['/api/ble/bonds/A4:C1:38:0B:7F:3A', '/api/ble/bonds/A4:C1:38:0B:7F:3A']);
  });
});
