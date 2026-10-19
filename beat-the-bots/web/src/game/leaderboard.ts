import type { Difficulty, RoundSummary } from '../engine/engine';
import { Keys, readJson, writeJson } from './storage';

export interface LeaderboardEntry {
  pnl: number; // cents
  returnBp: number;
  seed: number;
  difficulty: Difficulty;
  scenario: string;
  beatPct: number;
  at: number; // epoch ms
}

const MAX_ENTRIES = 10;

export function loadLeaderboard(): LeaderboardEntry[] {
  const v = readJson<unknown>(Keys.leaderboard, []);
  return Array.isArray(v) ? (v.filter(isEntry) as LeaderboardEntry[]) : [];
}

function isEntry(e: unknown): boolean {
  if (typeof e !== 'object' || e === null) return false;
  const r = e as Record<string, unknown>;
  return typeof r.pnl === 'number' && typeof r.seed === 'number' && typeof r.difficulty === 'string';
}

export function beatPct(s: Pick<RoundSummary, 'botsBeaten' | 'botCount'>): number {
  return s.botCount ? Math.round((100 * s.botsBeaten) / s.botCount) : 0;
}

/** Records a round; returns the updated board and the entry's rank (1-based, or 0 if it did not place). */
export function recordResult(
  board: LeaderboardEntry[],
  s: RoundSummary,
  now = Date.now(),
): { board: LeaderboardEntry[]; rank: number } {
  const entry: LeaderboardEntry = {
    pnl: s.pnl,
    returnBp: s.returnBp,
    seed: s.seed,
    difficulty: s.difficulty,
    scenario: s.scenario,
    beatPct: beatPct(s),
    at: now,
  };
  const next = [...board, entry].sort((a, b) => b.pnl - a.pnl || a.at - b.at).slice(0, MAX_ENTRIES);
  const rank = next.indexOf(entry) + 1;
  writeJson(Keys.leaderboard, next);
  return { board: next, rank };
}
