// The access token at rest. storage.local can be read from content scripts (they run in web
// pages' renderers), so the token is kept there only sealed with AES-GCM under a key that cannot
// be exported and lives in the extension's own IndexedDB, which no web page or content script can
// open. Service worker only.

export interface Sealed {
  iv: number[];
  ct: number[];
}

const DB = 'keyra';
const STORE = 'keys';
const KEY_ID = 'token';

function db(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(DB, 1);
    req.onupgradeneeded = () => req.result.createObjectStore(STORE);
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

function run<T>(mode: IDBTransactionMode, fn: (s: IDBObjectStore) => IDBRequest<T>): Promise<T> {
  return db().then(
    (d) =>
      new Promise<T>((resolve, reject) => {
        const tx = d.transaction(STORE, mode);
        const req = fn(tx.objectStore(STORE));
        tx.oncomplete = () => {
          d.close();
          resolve(req.result);
        };
        tx.onerror = tx.onabort = () => {
          d.close();
          reject(tx.error ?? req.error);
        };
      }),
  );
}

let keyPromise: Promise<CryptoKey> | null = null;

function key(create: boolean): Promise<CryptoKey> {
  keyPromise ??= (async () => {
    const found = await run<CryptoKey | undefined>('readonly', (s) => s.get(KEY_ID));
    if (found) return found;
    if (!create) throw new Error('no key');
    const k = await crypto.subtle.generateKey({ name: 'AES-GCM', length: 256 }, false, ['encrypt', 'decrypt']);
    await run('readwrite', (s) => s.put(k, KEY_ID));
    return k;
  })();
  return keyPromise.catch((e) => {
    keyPromise = null;
    throw e;
  });
}

const AAD = new TextEncoder().encode('keyra/companion/token/v1');

export async function seal(token: string): Promise<Sealed> {
  const iv = crypto.getRandomValues(new Uint8Array(12));
  const ct = await crypto.subtle.encrypt({ name: 'AES-GCM', iv, additionalData: AAD }, await key(true), new TextEncoder().encode(token));
  return { iv: [...iv], ct: [...new Uint8Array(ct)] };
}

/** The token, or null when it cannot be opened (key gone, data damaged): the user connects again. */
export async function unseal(s: Sealed): Promise<string | null> {
  try {
    const pt = await crypto.subtle.decrypt({ name: 'AES-GCM', iv: new Uint8Array(s.iv), additionalData: AAD }, await key(false), new Uint8Array(s.ct));
    return new TextDecoder().decode(pt);
  } catch (e) {
    console.warn('Keyra: the stored token cannot be opened; connect again', e);
    return null;
  }
}
