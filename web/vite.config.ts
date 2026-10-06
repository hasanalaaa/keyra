import { defineConfig } from 'vitest/config';
import preact from '@preact/preset-vite';
import { viteSingleFile } from 'vite-plugin-singlefile';

const mock = 'http://localhost:8787';

export default defineConfig({
  plugins: [
    preact(),
    // base '/': the device serves the manifest and icons only at these absolute paths (SPEC §5 Static).
    viteSingleFile({ removeViteModuleLoader: true, overrideConfig: { base: '/' } }),
  ],
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    target: ['safari16', 'chrome110', 'firefox115'],
    assetsInlineLimit: () => true,
    cssCodeSplit: false,
    modulePreload: false,
    reportCompressedSize: false,
  },
  // Keep the browser's Host so the mock's same-origin check (like the device's) accepts the dev origin.
  server: { proxy: { '/api': { target: mock, changeOrigin: false }, '/__mock': { target: mock, changeOrigin: false } } },
  test: { include: ['test/**/*.test.ts'], environment: 'node' },
});
