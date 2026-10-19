import { describe, expect, it } from 'vitest';
import { CandleBuilder } from '../src/game/candles';

describe('CandleBuilder', () => {
  it('aggregates trades into 1-second OHLCV candles', () => {
    const b = new CandleBuilder(100);
    b.addTrade(100_000, 101, 2);
    b.addTrade(500_000, 99, 1);
    b.addTrade(900_000, 100, 3);
    expect(b.candles).toEqual([{ time: 0, open: 100, high: 101, low: 99, close: 100, volume: 6 }]);
  });

  it('fills quiet seconds with flat candles at the previous close', () => {
    const b = new CandleBuilder(100);
    b.addTrade(200_000, 104, 1);
    b.advanceTo(3_100_000);
    expect(b.candles.map((c) => c.time)).toEqual([0, 1, 2, 3]);
    expect(b.candles[3]).toMatchObject({ open: 104, high: 104, low: 104, close: 104, volume: 0 });
    b.addTrade(3_500_000, 102, 1);
    expect(b.candles[3]).toMatchObject({ open: 104, high: 104, low: 102, close: 102, volume: 1 });
  });
});
