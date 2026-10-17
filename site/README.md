# Website

Keyra's website and browser installer, published to <https://hasanalaaa.github.io/keyra/> (Arabic at `/ar/`) by
[.github/workflows/pages.yml](../.github/workflows/pages.yml). Static HTML and CSS with a little vanilla JS; no
framework, no npm install.

| Path | What it is |
|---|---|
| `index.html`, `ar/index.html` | The page in English and Arabic (RTL). Edit both together. |
| `assets/site.css`, `assets/site.js` | Styles (tokens from [docs/brand/BRAND.md](../docs/brand/BRAND.md)) and the press demo / board picker. |
| `build.mjs` | Assembles everything into `site/dist`: pages, images, font, firmware and one ESP Web Tools manifest per board. |

## Preview locally

```sh
# with the latest release's firmware (needs gh)
gh release download -R hasanalaaa/keyra -D /tmp/keyra-release
node site/build.mjs --release /tmp/keyra-release --version "$(gh release view -R hasanalaaa/keyra --json tagName -q .tagName)"

# or without firmware: the installer lists the boards, disabled
node site/build.mjs

python3 -m http.server -d site/dist 8000   # any static server; open http://localhost:8000
```

`localhost` counts as a secure origin, so Chrome or Edge can flash a board from the local preview too.

## How the build works

- **Boards** come from [firmware/boards/boards.json](../firmware/boards/boards.json). A board is installable when
  the release has its app: `keyra-firmware.bin` for the default board, `keyra-firmware-<id>.bin` for the others.
  The rest stay listed as "Not in this release yet".
- **Firmware** goes to `dist/firmware/<tag>/`, checked against the release's `SHA256SUMS`; the offsets come from
  its `flash-offsets.txt`. Each board gets `dist/firmware/<id>.json` (chipFamily ESP32-S3,
  `new_install_prompt_erase: false`, so ESP Web Tools always erases first). The binaries are served from Pages
  because GitHub release downloads send no CORS headers.
- **ESP Web Tools** loads from unpkg at a pinned version, and an import map pins the sha384 of every module it
  can load; a changed file on the CDN refuses to load. To upgrade, `npm pack esp-web-tools@<version>`, hash
  `package/dist/web/*.js` and update `EWT` and `EWT_SRI` in `build.mjs`.
- **Images** are copied at build time from `docs/images`, `docs/brand`, `web/screenshots` and
  `hardware/enclosure/previews`. The product photos are concept renders and must stay captioned as such.
- Markers the build fills: `<!-- @esp-web-tools -->`, `<!-- @boards:table -->`, `<!-- @boards:picker -->`,
  `{{version}}`, `{{release_url}}`. The build fails if any is left over, a file is missing or a checksum differs.

## Deploying

The workflow runs on every published release, on manual dispatch and on pushes to `main` that touch the site,
boards.json or the enclosure previews. It needs **Settings → Pages → Source: GitHub Actions** once.
