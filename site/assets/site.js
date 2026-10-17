// Keyra website: the press demo and the installer's board picker. No framework, no build step.

// The press: blue while waiting, the line closes on the press (BRAND §5). Under reduced motion it never
// presses by itself; the visitor's own press still works.
const demo = document.querySelector('[data-press]');
if (demo) {
  const status = demo.querySelector('[data-press-status]');
  const button = demo.querySelector('[data-press-btn]');
  const reduce = matchMedia('(prefers-reduced-motion: reduce)');
  let timer = 0;
  const set = (mode) => {
    demo.dataset.mode = mode;
    button.disabled = mode !== 'wait';
    status.textContent = status.dataset[mode];
  };
  const wait = () => {
    clearTimeout(timer);
    set('wait');
    if (!reduce.matches) timer = setTimeout(press, 6000);
  };
  const press = () => {
    clearTimeout(timer);
    set('done');
    timer = setTimeout(wait, 3200);
  };
  button.addEventListener('click', press);
  // Run the loop only while the demo is on screen.
  new IntersectionObserver(([entry]) => {
    if (entry.isIntersecting) wait();
    else clearTimeout(timer);
  }, { threshold: 0.3 }).observe(demo);
}

// The installer: the board picks the manifest; the button stays off until the erase warning is acknowledged.
const installer = document.querySelector('[data-installer]');
if (installer) {
  const espButton = installer.querySelector('esp-web-install-button');
  const go = installer.querySelector('[data-install-btn]');
  const ack = installer.querySelector('[data-ack]');
  const name = installer.querySelector('[data-board-name]');
  const radios = [...installer.querySelectorAll('input[name="board"]')];
  const update = () => {
    const board = radios.find((r) => r.checked && !r.disabled);
    if (board) espButton.setAttribute('manifest', board.dataset.manifest);
    if (name) name.textContent = board ? board.dataset.name : '';
    go.disabled = !board || !ack.checked;
  };
  radios.forEach((r) => r.addEventListener('change', update));
  ack.addEventListener('change', update);
  update();
}
