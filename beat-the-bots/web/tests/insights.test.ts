import { describe, expect, it } from 'vitest';
import { botLabel, botResults, fillEdge } from '../src/game/insights';
import { Side } from '../src/engine/layout';

describe('botResults', () => {
  const bots = [
    { kind: 'informed', pnl: 12_000_000 },
    { kind: 'noise', pnl: -50_000 },
    { kind: 'noise', pnl: -150_000 },
    { kind: 'market maker', pnl: 300_000 },
    { kind: 'momentum', pnl: 20_000 },
  ];

  it('groups by type in a fixed order, Insider last', () => {
    const rows = botResults({ bots, pnl: 100_000 });
    expect(rows.map((r) => r.label)).toEqual(['Market makers', 'Momentum', 'Noise', 'Insider']);
    const noise = rows.find((r) => r.kind === 'noise')!;
    expect(noise).toMatchObject({ count: 2, avg: -100_000, best: -50_000, beaten: 2, beatAvg: true });
    expect(rows.find((r) => r.kind === 'informed')).toMatchObject({ beatAvg: false, beaten: 0 });
  });

  it('renames the informed bot for display', () => {
    expect(botLabel('informed')).toBe('Insider');
    expect(botLabel('something new')).toBe('something new');
  });
});

describe('fillEdge', () => {
  it('scores buys below and sells above fair value as positive edge', () => {
    const fv = [100, 104, 108];
    const e = fillEdge(
      [
        { timeUs: 1_200_000, price: 102, qty: 2, side: Side.Buy }, // +2 x2
        { timeUs: 2_000_000, price: 106, qty: 1, side: Side.Sell }, // -2
      ],
      fv,
    )!;
    expect(e.contracts).toBe(3);
    expect(e.goodContracts).toBe(2);
    expect(e.avgEdgeTicks).toBeCloseTo(2 / 3);
  });

  it('is null without fills', () => {
    expect(fillEdge([], [1])).toBeNull();
  });
});
