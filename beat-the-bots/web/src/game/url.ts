import type { Difficulty } from '../engine/engine';

export interface LaunchParams {
  seed: number | null;
  difficulty: Difficulty | null;
  scenario: string | null;
  roundSeconds: number | null; // hidden: short rounds for tests and demos
}

const DIFFS: Difficulty[] = ['easy', 'normal', 'hard'];
export const MAX_SEED = 999_999;

export function parseLaunch(search: string): LaunchParams {
  const q = new URLSearchParams(search);
  const seed = Number(q.get('seed'));
  const diff = q.get('diff');
  const t = Number(q.get('t'));
  const scenario = q.get('scenario');
  return {
    seed: Number.isInteger(seed) && seed > 0 && seed <= MAX_SEED ? seed : null,
    difficulty: DIFFS.includes(diff as Difficulty) ? (diff as Difficulty) : null,
    scenario: scenario && /^[a-z_]{1,32}$/.test(scenario) ? scenario : null,
    roundSeconds: Number.isInteger(t) && t >= 15 && t <= 3600 ? t : null,
  };
}

export function shareUrl(base: string, seed: number, difficulty: Difficulty, scenario?: string | null): string {
  const u = new URL(base);
  u.search = '';
  u.hash = '';
  u.searchParams.set('seed', String(seed));
  u.searchParams.set('diff', difficulty);
  if (scenario && scenario !== 'none') u.searchParams.set('scenario', scenario);
  return u.toString();
}

export function randomSeed(): number {
  return 1 + Math.floor(Math.random() * MAX_SEED);
}
