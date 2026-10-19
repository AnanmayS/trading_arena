import { useCallback, useEffect, useState, useSyncExternalStore } from 'react';
import type { GameController } from './controller';
import { Keys, readJson, writeJson } from './storage';

/** Re-renders the caller once per engine frame. */
export function useFrame(ctrl: GameController): number {
  return useSyncExternalStore(ctrl.subscribe, ctrl.getVersion);
}

export interface Prefs {
  colorblind: boolean;
  showStats: boolean;
  size: number;
}

const DEFAULT_PREFS: Prefs = { colorblind: false, showStats: false, size: 1 };

export function usePrefs(): [Prefs, (p: Partial<Prefs>) => void] {
  const [prefs, setPrefs] = useState<Prefs>(() => ({ ...DEFAULT_PREFS, ...readJson<Partial<Prefs>>(Keys.prefs, {}) }));
  useEffect(() => {
    writeJson(Keys.prefs, prefs);
    document.documentElement.dataset.palette = prefs.colorblind ? 'cb' : 'default';
  }, [prefs]);
  const update = useCallback((p: Partial<Prefs>) => setPrefs((old) => ({ ...old, ...p })), []);
  return [prefs, update];
}

/** Tracks a CSS media query (used to mount only one of the desktop / mobile layouts). */
export function useMediaQuery(query: string): boolean {
  const [match, setMatch] = useState(() => window.matchMedia(query).matches);
  useEffect(() => {
    const mq = window.matchMedia(query);
    const on = () => setMatch(mq.matches);
    mq.addEventListener('change', on);
    on();
    return () => mq.removeEventListener('change', on);
  }, [query]);
  return match;
}

/** Reads a CSS custom property (theme colours for canvas drawing). */
export function cssVar(name: string): string {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}
