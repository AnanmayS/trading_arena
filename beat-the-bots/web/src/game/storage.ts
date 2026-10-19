// localStorage that never throws. Private windows, blocked storage and
// sandboxed previews all make the accessor throw or return junk; the game must
// keep working with nothing persisted.

export function readJson<T>(key: string, fallback: T): T {
  try {
    const raw = window.localStorage.getItem(key);
    return raw == null ? fallback : (JSON.parse(raw) as T);
  } catch {
    return fallback;
  }
}

export function writeJson(key: string, value: unknown): boolean {
  try {
    window.localStorage.setItem(key, JSON.stringify(value));
    return true;
  } catch {
    return false;
  }
}

export const Keys = {
  tutorialDone: 'btb.tutorialDone.v1',
  leaderboard: 'btb.leaderboard.v1',
  prefs: 'btb.prefs.v1',
} as const;
