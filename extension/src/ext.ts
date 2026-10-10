// The WebExtension API: `browser` in Firefox (promises), `chrome` in Chromium (promises in MV3).
export const ext: typeof chrome = (globalThis as unknown as { browser?: typeof chrome }).browser ?? chrome;
