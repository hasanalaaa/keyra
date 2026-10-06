// Bottom sheet (phone) / centred dialog (desktop) and alert dialog (DESIGN §4.9).
import { render, type ComponentChildren } from 'preact';
import { useEffect, useLayoutEffect, useRef, useState } from 'preact/hooks';
import { IconButton, Button } from './ui';
import { t } from '../lib/i18n';

// ---------- layer stack: everything below the top modal layer is inert ----------

const stack: HTMLElement[] = [];

function syncInert(): void {
  const modal = stack.filter((el) => el.dataset.modal === '1');
  const topModal = modal[modal.length - 1];
  const main = document.getElementById('main');
  if (main) main.inert = !!topModal;
  const topIdx = topModal ? stack.indexOf(topModal) : -1;
  stack.forEach((el, i) => (el.inert = i < topIdx));
  document.documentElement.classList.toggle('scroll-lock', !!topModal);
}

/** Minimal portal: renders children into a body-level layer (keeps sheets outside the inert main). */
function Layer({ modal, children }: { modal: boolean; children: ComponentChildren }) {
  const el = useRef<HTMLDivElement | null>(null);
  if (!el.current) {
    el.current = document.createElement('div');
    el.current.className = 'layer';
    el.current.dataset.modal = modal ? '1' : '0';
  }
  useLayoutEffect(() => {
    const node = el.current!;
    document.body.appendChild(node);
    stack.push(node);
    syncInert();
    return () => {
      render(null, node);
      node.remove();
      stack.splice(stack.indexOf(node), 1);
      syncInert();
    };
  }, []);
  useLayoutEffect(() => {
    render(<>{children}</>, el.current!);
  });
  return null;
}

export const isDesktop = (): boolean => matchMedia('(min-width: 900px)').matches;

export function useMedia(q: string): boolean {
  const [m, setM] = useState(() => matchMedia(q).matches);
  useEffect(() => {
    const mq = matchMedia(q);
    const on = () => setM(mq.matches);
    mq.addEventListener('change', on);
    return () => mq.removeEventListener('change', on);
  }, [q]);
  return m;
}

function useFocusReturn(): void {
  useLayoutEffect(() => {
    const prev = document.activeElement as HTMLElement | null;
    return () => {
      if (prev && document.contains(prev)) prev.focus({ preventScroll: true });
    };
  }, []);
}

function focusFirst(root: HTMLElement | null, selector?: string): void {
  if (!root) return;
  const el =
    (selector && root.querySelector<HTMLElement>(selector)) ||
    root.querySelector<HTMLElement>('[autofocus], input:not([type=hidden]), textarea') ||
    root.querySelector<HTMLElement>('button, [href], select, [tabindex]:not([tabindex="-1"])');
  el?.focus({ preventScroll: true });
}

// ---------- sheet ----------

export interface SheetCtl {
  close: () => void;
}

export interface SheetProps {
  title: ComponentChildren;
  onClose: () => void;
  /** Return false to veto a user-initiated close (e.g. "Discard changes?"). */
  canClose?: () => boolean;
  start?: ComponentChildren;
  size?: 'sm' | 'md' | 'lg';
  tall?: boolean;
  modal?: boolean;
  dismissible?: boolean;
  ctl?: { current: SheetCtl | null };
  hideTitle?: boolean;
  children: ComponentChildren;
}

let sheetSeq = 0;

export function Sheet(props: SheetProps) {
  return (
    <Layer modal={props.modal !== false}>
      <SheetBody {...props} />
    </Layer>
  );
}

function SheetBody({ title, onClose, canClose, start, size = 'md', tall, modal = true, dismissible = true, ctl, hideTitle, children }: SheetProps) {
  const [closing, setClosing] = useState(false);
  const panel = useRef<HTMLDivElement>(null);
  const [titleId] = useState(() => `sheet-t-${++sheetSeq}`);
  const drag = useRef<{ y: number; t: number; dy: number } | null>(null);
  useFocusReturn();

  const close = () => {
    if (closing) return;
    setClosing(true);
    const reduce = matchMedia('(prefers-reduced-motion: reduce)').matches;
    setTimeout(onClose, reduce ? 120 : 200);
  };
  const userClose = () => {
    if (!dismissible) return;
    if (canClose && !canClose()) return;
    close();
  };
  if (ctl) ctl.current = { close };

  useEffect(() => focusFirst(panel.current?.querySelector<HTMLElement>('.sheet-body') ?? null), []);
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape' && stack[stack.length - 1]?.contains(panel.current)) {
        e.preventDefault();
        userClose();
      }
    };
    document.addEventListener('keydown', onKey);
    return () => document.removeEventListener('keydown', onKey);
  });

  // Drag-to-dismiss from grabber/header (phone only): 96 px or > 0.5 px/ms.
  const onPointerDown = (e: PointerEvent) => {
    if (isDesktop() || !dismissible || (e.target as HTMLElement).closest('button')) return;
    drag.current = { y: e.clientY, t: performance.now(), dy: 0 };
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
  };
  const onPointerMove = (e: PointerEvent) => {
    const d = drag.current;
    if (!d || !panel.current) return;
    d.dy = e.clientY - d.y;
    const y = d.dy > 0 ? d.dy : d.dy / 4;
    panel.current.style.transition = 'none';
    panel.current.style.transform = `translateY(${y}px)`;
  };
  const onPointerUp = () => {
    const d = drag.current;
    drag.current = null;
    if (!d || !panel.current) return;
    const v = d.dy / Math.max(1, performance.now() - d.t);
    panel.current.style.transition = '';
    panel.current.style.transform = '';
    if (d.dy > 96 || v > 0.5) userClose();
  };

  return (
    <div class={`sheet-wrap${closing ? ' closing' : ''}${modal ? '' : ' non-modal'}`}>
      {modal && <div class="scrim" onClick={userClose} />}
      <div
        ref={panel}
        class={`sheet sheet-${size}${tall ? ' sheet-tall' : ''}`}
        role="dialog"
        aria-modal={modal}
        aria-labelledby={titleId}
      >
        <header class="sheet-head glass" onPointerDown={onPointerDown} onPointerMove={onPointerMove} onPointerUp={onPointerUp} onPointerCancel={onPointerUp}>
          <span class="grabber" aria-hidden="true" />
          <div class="sheet-start">{start}</div>
          <h2 class={`sheet-title${hideTitle ? ' sr-only' : ''}`} id={titleId}>
            {title}
          </h2>
          <div class="sheet-end">{dismissible && <IconButton icon="x" label={t('close')} onClick={userClose} />}</div>
        </header>
        <div class="sheet-body">{children}</div>
      </div>
    </div>
  );
}

// ---------- alert dialog ----------

export interface AlertAction {
  label: string;
  variant?: 'primary' | 'danger-confirm' | 'secondary';
  run: () => void;
}

export function Alert({ title, body, actions, onCancel, cancelLabel }: { title: string; body?: ComponentChildren; actions: AlertAction[]; onCancel: () => void; cancelLabel?: string }) {
  return (
    <Layer modal>
      <AlertBody title={title} body={body} actions={actions} onCancel={onCancel} cancelLabel={cancelLabel} />
    </Layer>
  );
}

function AlertBody({ title, body, actions, onCancel, cancelLabel }: { title: string; body?: ComponentChildren; actions: AlertAction[]; onCancel: () => void; cancelLabel?: string }) {
  const box = useRef<HTMLDivElement>(null);
  const [ids] = useState(() => ++sheetSeq);
  useFocusReturn();
  useEffect(() => {
    focusFirst(box.current, '.alert-cancel');
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape' && stack[stack.length - 1]?.contains(box.current)) {
        e.preventDefault();
        onCancel();
      }
    };
    document.addEventListener('keydown', onKey);
    return () => document.removeEventListener('keydown', onKey);
  }, []);
  return (
    <div class="sheet-wrap alert-wrap">
      <div class="scrim" onClick={onCancel} />
      <div ref={box} class="alert" role="alertdialog" aria-modal="true" aria-labelledby={`al-t-${ids}`} aria-describedby={body ? `al-b-${ids}` : undefined}>
        <h2 class="alert-title" id={`al-t-${ids}`}>
          {title}
        </h2>
        {body && (
          <p class="alert-body" id={`al-b-${ids}`}>
            {body}
          </p>
        )}
        <div class="alert-actions">
          {actions.map((a) => (
            <Button key={a.label} variant={a.variant ?? 'primary'} full onClick={a.run}>
              {a.label}
            </Button>
          ))}
          <Button variant="secondary" full class="alert-cancel" onClick={onCancel}>
            {cancelLabel ?? t('cancel')}
          </Button>
        </div>
      </div>
    </div>
  );
}
