// Hands a generated password from the Generate sheet to a new account's form without putting it
// in the URL or in storage (DESIGN §4.6): one module variable, taken (and so cleared) once.
let draft = '';

export function setDraftPassword(pw: string): void {
  draft = pw;
}

export function takeDraftPassword(): string {
  const pw = draft;
  draft = '';
  return pw;
}
