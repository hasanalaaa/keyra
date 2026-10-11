// Onboarding's in-memory draft, outside the lazily loaded Setup screen so the app shell can ask
// whether setup is still running.
// In memory only (never persisted): survives step changes, not reloads.
// `sent`: this tab asked the device to set up, so `initialized` turning true is our own commit and the
// app must stay here for the done/reconnect screen (the poll can report it before the result is read).
export const draft = { passphrase: '', confirm: '', wifi: '', sent: false, committed: false };

/** True while the just-initialized device still needs the done/reconnect screen. */
export const setupInProgress = (): boolean => draft.sent || draft.committed;
