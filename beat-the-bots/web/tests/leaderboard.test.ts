import { afterEach, describe, expect, it, vi } from 'vitest';
import { loadLeaderboard, recordResult } from '../src/game/leaderboard';
import type { RoundSummary } from '../src/engine/engine';

const summary = (pnl: number, seed = 1): RoundSummary => ({
  seed,
  difficulty: 'normal',
  scenario: 'none',
  pnl,
  returnBp: 0,
  maxDrawdown: 0,
  fills: 1,
  closes: 1,
  wins: 1,
  botsBeaten: 16,
  botCount: 32,
  liquidations: 0,
  goalMet: 1,
  settlementPrice: 20000,
  fairValue: 20000,
  bots: [],
  equity: [],
});

afterEach(() => {
  vi.restoreAllMocks();
  window.localStorage.clear();
});

describe('leaderboard', () => {
  it('keeps the best ten rounds sorted by P&L and reports the rank', () => {
    let board = loadLeaderboard();
    for (let i = 0; i < 12; i++) board = recordResult(board, summary(i * 100, i)).board;
    expect(board).toHaveLength(10);
    expect(board[0].pnl).toBe(1100);
    const { rank } = recordResult(board, summary(550, 99));
    expect(rank).toBe(7);
    expect(loadLeaderboard()[6].seed).toBe(99);
    expect(loadLeaderboard()[0].beatPct).toBe(50);
  });

  it('survives storage that throws or holds garbage', () => {
    window.localStorage.setItem('btb.leaderboard.v1', '{not json');
    expect(loadLeaderboard()).toEqual([]);
    vi.spyOn(Storage.prototype, 'getItem').mockImplementation(() => {
      throw new Error('blocked');
    });
    vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => {
      throw new Error('blocked');
    });
    expect(loadLeaderboard()).toEqual([]);
    expect(recordResult([], summary(5)).rank).toBe(1);
  });
});
