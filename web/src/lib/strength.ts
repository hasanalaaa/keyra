// Strength estimate for the 4-segment meter (DESIGN §4.7).

const COMMON = new Set([
  'password', '123456', '12345678', '123456789', '1234567890', 'qwerty', 'qwertyuiop', 'keyra1234',
  'iloveyou', 'admin', 'welcome', 'letmein', 'monkey', 'dragon', 'football', 'baseball', 'abc123',
  '111111', '000000', '123123', '654321', 'sunshine', 'princess', 'master', 'shadow', 'superman',
  'trustno1', 'passw0rd', 'password1', 'password123', 'qwerty123', '1q2w3e4r', 'zaq12wsx', 'starwars',
  'whatever', 'freedom', 'hello123', 'login', 'access', 'secret', 'michael', 'charlie', 'jordan',
  'mustang', 'batman', 'computer', 'internet', 'samsung', 'google', 'asdfghjkl',
]);

export type Strength = 0 | 1 | 2 | 3 | 4; // 0 = empty

export function bits(pw: string): number {
  if (!pw) return 0;
  const chars = Array.from(pw);
  let pool = 0;
  if (/[a-z]/.test(pw)) pool += 26;
  if (/[A-Z]/.test(pw)) pool += 26;
  if (/[0-9]/.test(pw)) pool += 10;
  if (/[!-/:-@[-`{-~ ]/.test(pw)) pool += 33;
  if (/[؀-ۿ]/.test(pw)) pool += 36;
  if (pool === 0) pool = 33; // other scripts: treat like symbols
  let b = chars.length * Math.log2(pool);
  // Penalties: runs of the same character (≥ 3) and ascending/descending sequences (abc, 321).
  const cp = chars.map((ch) => ch.toLowerCase().codePointAt(0)!);
  let run = 1;
  let seq = 1;
  let prev = 0;
  for (let i = 1; i < cp.length; i++) {
    const d = cp[i] - cp[i - 1];
    run = d === 0 ? run + 1 : 1;
    if (run === 3) b -= 8;
    if (d === 1 || d === -1) seq = seq > 1 && d === prev ? seq + 1 : 2;
    else seq = 1;
    if (seq === 3) b -= 8;
    prev = d;
  }
  return Math.max(0, b);
}

/** DESIGN §4.7 bands. Also used for the generator's exact entropy. */
export function levelOfBits(b: number): Exclude<Strength, 0> {
  if (b < 36) return 1;
  if (b < 60) return 2;
  if (b < 80) return 3;
  return 4;
}

export function strength(pw: string): Strength {
  if (!pw) return 0;
  if (COMMON.has(pw.toLowerCase())) return 1;
  return levelOfBits(bits(pw));
}
