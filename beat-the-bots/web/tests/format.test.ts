import { describe, expect, it } from 'vitest';
import { centsToDollars, formatClock, pct, signedDollars, ticksToPoints } from '../src/engine/format';

describe('display formatting', () => {
  it('converts ticks to index points', () => {
    expect(ticksToPoints(20000)).toBe('5000.00');
    expect(ticksToPoints(20001)).toBe('5000.25');
    expect(ticksToPoints(19999)).toBe('4999.75');
  });
  it('formats money from integer cents', () => {
    expect(centsToDollars(2_500_000)).toBe('$25,000.00');
    expect(signedDollars(-8100)).toBe('-$81.00');
    expect(signedDollars(1250)).toBe('+$12.50');
    expect(signedDollars(0)).toBe('$0.00');
  });
  it('formats the clock and returns', () => {
    expect(formatClock(300_000_000)).toBe('5:00');
    expect(formatClock(59_100_000)).toBe('1:00');
    expect(formatClock(-5)).toBe('0:00');
    expect(pct(-32)).toBe('-0.32%');
    expect(pct(150)).toBe('+1.50%');
  });
});
