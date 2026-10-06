export function migrationUri(
  accounts: { secret: Buffer; name: string; issuer?: string; algorithm?: number; digits?: number; type?: number }[],
  batch?: { size: number; index: number; id: number },
): string;
export function qrPng(text: string): Promise<Buffer>;
