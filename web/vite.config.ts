import { defineConfig } from 'vitest/config';
import preact from '@preact/preset-vite';
import { viteSingleFile } from 'vite-plugin-singlefile';

const mock = 'http://localhost:8787';

export default defineConfig({
  plugins: [preact(), viteSingleFile({ removeViteModuleLoader: true })],
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    target: ['safari16', 'chrome110', 'firefox115'],
    assetsInlineLimit: () => true,
    cssCodeSplit: false,
    modulePreload: false,
    reportCompressedSize: false,
  },
  server: { proxy: { '/api': mock, '/__mock': mock } },
  test: { include: ['test/**/*.test.ts'], environment: 'node' },
});
