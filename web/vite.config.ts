import { defineConfig } from 'vitest/config';
import preact from '@preact/preset-vite';

const mock = 'http://localhost:8787';

export default defineConfig({
  plugins: [
    preact(),
    {
      // The font is the one asset every screen needs: start it with the HTML, not after the script runs.
      name: 'keyra-preload-font',
      transformIndexHtml: {
        order: 'post',
        handler(_html, ctx) {
          const font = Object.keys(ctx.bundle ?? {}).find((f) => f.endsWith('.woff2'));
          if (!font) return [];
          return [{ tag: 'link', attrs: { rel: 'preload', href: `/${font}`, as: 'font', type: 'font/woff2', crossorigin: '' }, injectTo: 'head' }];
        },
      },
    },
  ],
  // base '/': the device serves the manifest, icons and /assets/* only at these absolute paths (SPEC §5 Static).
  base: '/',
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    target: ['safari16', 'chrome110', 'firefox115'],
    // Separate hashed files (SPEC §5 Static): nothing inlined, so the CSP needs no 'unsafe-inline'.
    assetsInlineLimit: 0,
    cssCodeSplit: false,
    modulePreload: false,
    reportCompressedSize: false,
  },
  // Keep the browser's Host so the mock's same-origin check (like the device's) accepts the dev origin.
  server: { proxy: { '/api': { target: mock, changeOrigin: false }, '/__mock': { target: mock, changeOrigin: false } } },
  test: { include: ['test/**/*.test.ts'], environment: 'node' },
});
