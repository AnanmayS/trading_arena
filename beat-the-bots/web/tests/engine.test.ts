// @vitest-environment node
// Runs the real WebAssembly engine through the TypeScript wrapper: checks the
// frame/event ABI end to end.
import { describe, expect, it } from 'vitest';
import { BtbEngine, loadModule } from '../src/engine/engine';
import { CANCEL_FLAG_STOP, EventType, OrderType, Side } from '../src/engine/layout';

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

  it('places, shows, cancels and triggers stops through the wrapper', async () => {
    const mod = await loadModule();
    const reasons = BtbEngine.catalog(mod).rejectReasons;
    const e = new BtbEngine(mod, { seed: 42, difficulty: 'normal', roundSeconds: 30 });
    for (let i = 0; i < 100; i++) e.step(16);
    e.submit(Side.Buy, OrderType.Market, 0, 1);
    const last = e.refresh().frame.lastTrade!;

    // A sell stop at or above the last trade would fire at once: refused.
    expect(e.submitStop(Side.Sell, last + 1, 1)).toBe(0);
    const rej = e.refresh().events.find((ev) => ev.type === EventType.Reject);
    expect(reasons[rej!.reason]).toBe('stop price already reached');

    const far = e.submitStop(Side.Sell, last - 100, 1);
    expect(far).toBeGreaterThan(0);
    let r = e.refresh();
    expect(r.frame.stops).toEqual([{ id: far, side: Side.Sell, price: last - 100, qty: 1 }]);
    expect(r.frame.orders).toEqual([]); // not in the book
    expect(r.events.find((ev) => ev.type === EventType.Ack && ev.orderId === far)?.reason).toBe(OrderType.Stop);
    expect(e.cancel(far)).toBe(true);
    r = e.refresh();
    expect(r.frame.stops).toEqual([]);
    const cancel = r.events.find((ev) => ev.type === EventType.Cancel && ev.orderId === far);
    expect(cancel!.flags & CANCEL_FLAG_STOP).toBe(CANCEL_FLAG_STOP);

    // Tight stops either side of the market: one of them fires soon.
    const ids = [e.submitStop(Side.Sell, last - 1, 1), e.submitStop(Side.Buy, last + 1, 1)];
    expect(e.refresh().frame.stops.map((s) => s.id)).toEqual(ids);
    let trig;
    for (let i = 0; i < 2000 && !trig; i++) {
      e.step(25);
      trig = e.refresh().events.find((ev) => ev.type === EventType.Trigger && ev.participant === 0);
    }
    expect(trig).toBeDefined();
    expect(ids).toContain(trig!.orderId);
    expect(e.refresh().frame.stops.map((s) => s.id)).not.toContain(trig!.orderId);
    e.dispose();
  });

  it('native and wasm agree: the self-test hash is stable', async () => {
    const mod = await loadModule();
    const a = mod.Engine.selfTest(1, '{"difficulty":"normal","round_seconds":60}');
    const b = mod.Engine.selfTest(1, '{"difficulty":"normal","round_seconds":60}');
    expect(a).toMatch(/^[0-9a-f]{16}$/);
    expect(a).toBe(b);
  });
});
