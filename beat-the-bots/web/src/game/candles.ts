// Aggregates trade prints into 1-second OHLC candles for the chart. Seconds
// with no trades get a flat candle at the previous close so time stays
// continuous on the x axis.

export interface Candle {
  time: number; // whole simulated seconds since the round started
  open: number; // ticks
  high: number;
  low: number;
  close: number;
  volume: number;
}

export class CandleBuilder {
  readonly candles: Candle[] = [];
  private lastClose: number;

  constructor(openingPrice: number) {
    this.lastClose = openingPrice;
  }

  /** Adds a trade; returns the candle it landed in. */
  addTrade(timeUs: number, price: number, qty: number): Candle {
    const c = this.candleAt(Math.max(0, Math.floor(timeUs / 1_000_000)));
    c.high = Math.max(c.high, price);
    c.low = Math.min(c.low, price);
    c.close = price;
    c.volume += qty;
    this.lastClose = price;
    return c;
  }

  /** Makes sure candles exist up to `timeUs` (flat ones if nothing traded). */
  advanceTo(timeUs: number): void {
    this.candleAt(Math.max(0, Math.floor(timeUs / 1_000_000)));
  }

  private candleAt(sec: number): Candle {
    let last = this.candles[this.candles.length - 1];
    if (last && last.time >= sec) return last;
    const from = last ? last.time + 1 : 0;
    for (let t = from; t <= sec; t++) {
      last = { time: t, open: this.lastClose, high: this.lastClose, low: this.lastClose, close: this.lastClose, volume: 0 };
      this.candles.push(last);
    }
    return last!;
  }
}
