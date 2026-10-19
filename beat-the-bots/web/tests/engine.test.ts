// @vitest-environment node
// Runs the real WebAssembly engine through the TypeScript wrapper: checks the
// frame/event ABI end to end.
import { describe, expect, it } from 'vitest';
import { BtbEngine, loadModule } from '../src/engine/engine';
import { EventType, OrderType, Side } from '../src/engine/layout';

describe('BtbEngine (wasm)', () => {
  it('runs a market and reads a consistent frame', async () => {
    const mod = await loadModule();
    const e = new BtbEngine(mod, { seed: 42, difficulty: 'normal', roundSeconds: 30 });
    const { frame } = e.refresh();
    expect(frame.roundUs).toBe(30_000_000);
    expect(frame.equity).toBe(2_500_000);
    expect(frame.bids.length).toBeGreaterThan(0);
    expect(frame.asks.length).toBeGreaterThan(0);
    expect(frame.bestBid).toBe(frame.bids[0].price);
    expect(frame.bestAsk).toBe(frame.asks[0].price);
    expect(frame.bestBid!).toBeLessThan(frame.bestAsk!);
    for (let i = 1; i < frame.bids.length; i++) expect(frame.bids[i].price).toBeLessThan(frame.bids[i - 1].price);

    for (let i = 0; i < 100; i++) e.step(16);
    const id = e.submit(Side.Buy, OrderType.Market, 0, 2);
    expect(id).toBeGreaterThan(0);
    const after = e.refresh();
    expect(after.frame.position).toBe(2);
    expect(after.frame.fees).toBe(200);
    const fills = after.events.filter((ev) => ev.type === EventType.Fill && ev.participant === 0);
    expect(fills.reduce((s, f) => s + f.qty, 0)).toBe(2);
    expect(after.events.some((ev) => ev.type === EventType.Ack && ev.orderId === id)).toBe(true);

    const resting = e.submit(Side.Sell, OrderType.Limit, after.frame.mark + 40, 1);
    expect(e.refresh().frame.orders).toEqual([{ id: resting, side: Side.Sell, price: after.frame.mark + 40, qty: 1 }]);
    expect(e.cancel(resting)).toBe(true);
    expect(e.refresh().frame.orders).toEqual([]);

    // A price outside the fat-finger band is rejected with a reason.
    expect(e.submit(Side.Buy, OrderType.Limit, after.frame.mark + 5000, 1)).toBe(0);
    const rej = e.refresh().events.find((ev) => ev.type === EventType.Reject);
    expect(BtbEngine.catalog(mod).rejectReasons[rej!.reason]).toBe('price outside band');

    while (!e.finished()) e.step(250);
    const s = e.summary();
    expect(s.seed).toBe(42);
    expect(s.botCount).toBe(32);
    expect(e.refresh().frame.position).toBe(0);
    e.dispose();
  });

  it('native and wasm agree: the self-test hash is stable', async () => {
    const mod = await loadModule();
    const a = mod.Engine.selfTest(1, '{"difficulty":"normal","round_seconds":60}');
    const b = mod.Engine.selfTest(1, '{"difficulty":"normal","round_seconds":60}');
    expect(a).toMatch(/^[0-9a-f]{16}$/);
    expect(a).toBe(b);
  });

  it('a replay from the action log reproduces the round, final P&L included', async () => {
    const mod = await loadModule();
    const opts = { seed: 314, difficulty: 'hard' as const, roundSeconds: 20 };
    const live = new BtbEngine(mod, opts);
    // Uneven frames and a mix of commands, like a real session.
    let i = 0;
    while (!live.finished()) {
      live.step(10 + ((i * 37) % 30));
      if (i % 25 === 3) live.submit(i % 2 ? Side.Sell : Side.Buy, OrderType.Market, 0, 1 + (i % 3));
      if (i % 60 === 7) {
        const mark = live.refresh().frame.mark;
        const id = live.submit(Side.Buy, OrderType.Limit, mark - 2, 1);
        if (id && i % 120 === 7) live.cancel(id);
      }
      if (i % 200 === 150) live.flatten();
      i++;
    }
    const before = live.summary();
    const log = live.actionLog();
    expect(JSON.parse(log).length).toBeGreaterThan(7 * 10);
    live.dispose();

    const replay = new BtbEngine(mod, opts, log);
    expect(replay.replaying()).toBe(true);
    while (!replay.finished()) replay.step(16 * 8); // 8x speed
    const after = replay.summary();
    expect(after.pnl).toBe(before.pnl);
    expect(after).toEqual(before);
    expect(replay.actionLog()).toBe(log);
    replay.dispose();
  });

  it('a malformed action log is reported as a config error', async () => {
    const mod = await loadModule();
    expect(() => new BtbEngine(mod, { seed: 1, difficulty: 'easy' }, '[1,2')).toThrow(/action log/);
  });

  it('withholds the hidden fair value until the round is over', async () => {
    const mod = await loadModule();
    const e = new BtbEngine(mod, { seed: 8, difficulty: 'normal', roundSeconds: 15 });
    for (let k = 0; k < 40; k++) e.step(100);
    const mid = e.summary();
    expect(mid.fairValues).toBeUndefined();
    expect(mid.fairValue).toBeUndefined();
    while (!e.finished()) e.step(250);
    const s = e.summary();
    expect(s.fairValues).toHaveLength(16);
    expect(s.fairValues.every((v) => Number.isInteger(v) && v > 0)).toBe(true);
    expect(s.fairValues[15]).toBe(s.fairValue);
    e.dispose();
  });
});
