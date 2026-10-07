// The system of each computer Keyra types into (SPEC §10.5) and this browser's
// note of whether that computer is in a non-Latin input language right now.
// Pure helpers, unit-tested in test/hostos.test.ts.
import { defaultTarget } from './ble';
import type { BleInfo, HostOs } from './types';

export const OS_LIST: Exclude<HostOs, ''>[] = ['mac', 'ios', 'windows', 'android', 'linux'];

/** A first guess from the name a Bluetooth host gives itself ("Hasan's iPhone", "MacBook Pro"). */
export function guessOs(name: string): HostOs {
  const n = name.toLowerCase();
  if (/iphone|ipad|ipod/.test(n)) return 'ios';
  if (/macbook|imac|mac mini|mac studio|mac pro|\bmac\b/.test(n)) return 'mac';
  if (/android|pixel|galaxy|redmi|xiaomi|oneplus|huawei|honor|oppo|vivo|\bsm-/.test(n)) return 'android';
  if (/desktop-|laptop-|windows|surface/.test(n)) return 'windows';
  return '';
}

/** The target an action will use: the user's pick, else the device's own choice. */
export function resolveTarget(chosen: string | null | undefined, output: 'usb' | 'ble' | null, ble: BleInfo | null): string | null {
  return chosen ?? defaultTarget(output, ble);
}

export function osOf(target: string | null, usbOs: HostOs | undefined, ble: BleInfo | null): HostOs {
  if (target === 'usb') return usbOs ?? '';
  return ble?.bonds.find((b) => b.addr === target)?.os ?? '';
}

/** macOS and iOS switch input language with Ctrl+Space; Keyra switches there and back. */
export const switchesLang = (os: HostOs): boolean => os === 'mac' || os === 'ios';

const LANG_KEY = 'keyra.otherLang.';

/** True when the user said this computer is usually in a non-Latin language (Arabic). */
export function otherLang(target: string | null): boolean {
  if (!target) return false;
  try {
    return localStorage.getItem(LANG_KEY + target) === '1';
  } catch {
    return false;
  }
}

export function setOtherLang(target: string, on: boolean): void {
  try {
    if (on) localStorage.setItem(LANG_KEY + target, '1');
    else localStorage.removeItem(LANG_KEY + target);
  } catch {
    // Private mode: the choice applies to this action only.
  }
}

/** What POST /api/type gets: switch only where the host supports it and the user said so. */
export function wantsSwitch(target: string | null, os: HostOs): boolean {
  return switchesLang(os) && otherLang(target);
}
