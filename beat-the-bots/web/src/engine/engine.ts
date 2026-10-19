// Typed wrapper around the WebAssembly engine.
//
// Per frame the UI calls step() then refresh(). refresh() asks the engine to
// write its snapshot and drain events into preallocated buffers, then reads
// them through typed-array views on WASM memory: two boundary crossings per
// frame no matter how big the book or how busy the tape.
import createBtbModule, { type BtbModule, type WasmEngine, type WasmStats } from './wasm/btb.js';
import {
  ASKS_OFFSET,
  OrderType,
  BIDS_OFFSET,
  EVENT_SIZE,
  EventField,
  FRAME_SIZE,
  FRAME_VERSION,
  ORDERS_OFFSET,
  STOPS_OFFSET,
  Slot,
  type EventTypeCode,
  type OrderTypeCode,
  type PhaseCode,
  type SideCode,
} from './layout';

export interface BookLevel {
  price: number;
  qty: number;
  orders: number;
}

export interface WorkingOrder {
  id: number;
  side: SideCode;
  price: number; // limit price, or trigger price for a stop
  qty: number;
}

export interface Frame {
  timeUs: number;
  roundUs: number;
  phase: PhaseCode;
  finished: boolean;
  bestBid: number | null;
  bestAsk: number | null;
  lastTrade: number | null;
  mark: number;
  position: number;
  openCost: number;
  realized: number; // cents
  unrealized: number; // cents
  equity: number; // cents
  initialMargin: number; // cents
  maintMargin: number; // cents
  ticksToLiq: number; // -1 when flat
  openBuy: number;
  openSell: number;
  liquidating: boolean;
  fees: number; // cents
  fills: number;
  totalPnl: number; // cents
  maxDrawdown: number; // cents
  startingCash: number; // cents
  tickValue: number; // cents
  ticksPerPoint: number;
  maxPosition: number;
  priceBand: number;
  liquidations: number;
  bids: BookLevel[];
  asks: BookLevel[];
  orders: WorkingOrder[];
  stops: WorkingOrder[];
}

export interface EngineEvent {
  timeUs: number;
  price: number;
  value: number;
  orderId: number;
  qty: number;
  participant: number;
  type: EventTypeCode;
  side: SideCode;
  reason: number;
  flags: number;
  aux: number;
}

export interface Headline {
  text: string;
  sentiment: number;
}

export interface ScenarioInfo {
  key: string;
  title: string;
  goal: string;
  seed: number;
  difficulty: Difficulty;
}

export interface Catalog {
  headlines: Headline[];
  scenarios: ScenarioInfo[];
  rejectReasons: string[];
  cancelReasons: string[];
}

export interface RoundSummary {
  seed: number;
  difficulty: Difficulty;
  scenario: string;
  pnl: number;
  returnBp: number;
  maxDrawdown: number;
  fills: number;
  closes: number;
  wins: number;
  botsBeaten: number;
  botCount: number;
  liquidations: number;
  goalMet: number;
  settlementPrice: number;
  fairValue: number; // ticks; the engine only reveals it once the round is finished
  bots: { kind: string; pnl: number }[];
  equity: number[]; // cents, index = second (plus a final post-settlement sample)
  fairValues: number[]; // hidden fair value in ticks, index = second
}

export type Difficulty = 'easy' | 'normal' | 'hard';

export interface GameOptions {
  seed: number;
  difficulty: Difficulty;
  scenario?: string;
  roundSeconds?: number;
}

let modulePromise: Promise<BtbModule> | null = null;

export function loadModule(): Promise<BtbModule> {
  modulePromise ??= createBtbModule();
  return modulePromise;
}

export class BtbEngine {
  private readonly mod: BtbModule;
  private readonly wasm: WasmEngine;
  private disposed = false;

  /**
   * With `replayLog` (from actionLog() of a finished round on the same options)
   * the engine re-plays that round by itself; player commands must not be sent.
   */
  constructor(mod: BtbModule, opts: GameOptions, replayLog?: string) {
    this.mod = mod;
    const config: Record<string, string | number> = { difficulty: opts.difficulty };
    if (opts.scenario) config.scenario = opts.scenario;
    if (opts.roundSeconds) config.round_seconds = opts.roundSeconds;
    this.wasm =
      replayLog === undefined
        ? new mod.Engine(opts.seed, JSON.stringify(config))
        : new mod.Engine(opts.seed, JSON.stringify(config), replayLog);
    const err = this.wasm.configError();
    if (err) throw new Error(`engine config: ${err}`);
    if (this.wasm.frameSize() !== FRAME_SIZE) throw new Error('engine frame layout mismatch: rebuild the wasm module');
  }

  static catalog(mod: BtbModule): Catalog {
    return JSON.parse(mod.Engine.catalog()) as Catalog;
  }

  step(ms: number): void {
    this.wasm.step(Math.round(ms));
  }

  /** Returns the order id, or 0 if the engine rejected it (a Reject event follows). */
  submit(side: SideCode, type: OrderTypeCode, priceTicks: number, qty: number): number {
    return this.wasm.submit(side, type, priceTicks, qty);
  }
  /** Stop-market: fires a market order once the last trade reaches `triggerTicks`. */
  submitStop(side: SideCode, triggerTicks: number, qty: number): number {
    return this.wasm.submit(side, OrderType.Stop, triggerTicks, qty);
  }
  /** Cancels a resting order or a working stop. */
  cancel(id: number): boolean {
    return this.wasm.cancel(id);
  }
  cancelAll(): number {
    return this.wasm.cancelAll();
  }
  flatten(): void {
    this.wasm.flatten();
  }

  /** Snapshot the engine: the current frame plus every event since the last refresh. */
  refresh(): { frame: Frame; events: EngineEvent[] } {
    const events: EngineEvent[] = [];
    let n = this.wasm.refresh();
    const frame = this.readFrame();
    for (;;) {
      this.readEvents(n, events);
      if (n < 8192) break;
      n = this.wasm.refresh(); // more pending than one drain holds
    }
    return { frame, events };
  }

  stats(): WasmStats {
    return this.wasm.getStats();
  }

  summary(): RoundSummary {
    return JSON.parse(this.wasm.summaryJson()) as RoundSummary;
  }

  finished(): boolean {
    return this.wasm.finished();
  }

  replaying(): boolean {
    return this.wasm.replaying();
  }

  /** Every player command so far, tick-stamped, as an opaque JSON string for replay. */
  actionLog(): string {
    return this.wasm.actionLogJson();
  }

  dispose(): void {
    if (!this.disposed) {
      this.wasm.delete();
      this.disposed = true;
    }
  }

  private readFrame(): Frame {
    // Views are recreated each read: memory growth replaces the ArrayBuffer.
    const f = new BigInt64Array(this.mod.HEAPU8.buffer, this.wasm.framePtr(), FRAME_SIZE);
    const at = (i: number) => Number(f[i]);
    if (at(Slot.Version) !== FRAME_VERSION) throw new Error('engine frame version mismatch');

    const levels = (offset: number, count: number): BookLevel[] => {
      const out: BookLevel[] = new Array(count);
      for (let i = 0; i < count; i++) {
        out[i] = { price: at(offset + i * 3), qty: at(offset + i * 3 + 1), orders: at(offset + i * 3 + 2) };
      }
      return out;
    };
    const working = (offset: number, count: number): WorkingOrder[] => {
      const out: WorkingOrder[] = new Array(count);
      for (let i = 0; i < count; i++) {
        const b = offset + i * 4;
        out[i] = { id: at(b), side: at(b + 1) as SideCode, price: at(b + 2), qty: at(b + 3) };
      }
      return out;
    };
    const nonZero = (v: number) => (v === 0 ? null : v);

    return {
      timeUs: at(Slot.TimeUs),
      roundUs: at(Slot.RoundUs),
      phase: at(Slot.Phase) as PhaseCode,
      finished: at(Slot.Finished) !== 0,
      bestBid: nonZero(at(Slot.BestBid)),
      bestAsk: nonZero(at(Slot.BestAsk)),
      lastTrade: nonZero(at(Slot.LastTrade)),
      mark: at(Slot.Mark),
      position: at(Slot.Position),
      openCost: at(Slot.OpenCost),
      realized: at(Slot.Realized),
      unrealized: at(Slot.Unrealized),
      equity: at(Slot.Equity),
      initialMargin: at(Slot.InitialMargin),
      maintMargin: at(Slot.MaintMargin),
      ticksToLiq: at(Slot.TicksToLiq),
      openBuy: at(Slot.OpenBuy),
      openSell: at(Slot.OpenSell),
      liquidating: at(Slot.Liquidating) !== 0,
      fees: at(Slot.Fees),
      fills: at(Slot.Fills),
      totalPnl: at(Slot.TotalPnl),
      maxDrawdown: at(Slot.MaxDrawdown),
      startingCash: at(Slot.StartingCash),
      tickValue: at(Slot.TickValue),
      ticksPerPoint: at(Slot.TicksPerPoint),
      maxPosition: at(Slot.MaxPosition),
      priceBand: at(Slot.PriceBand),
      liquidations: at(Slot.Liquidations),
      bids: levels(BIDS_OFFSET, at(Slot.BidCount)),
      asks: levels(ASKS_OFFSET, at(Slot.AskCount)),
      orders: working(ORDERS_OFFSET, at(Slot.OrderCount)),
      stops: working(STOPS_OFFSET, at(Slot.StopCount)),
    };
  }

  private readEvents(count: number, out: EngineEvent[]): void {
    const view = new DataView(this.mod.HEAPU8.buffer, this.wasm.eventsPtr(), count * EVENT_SIZE);
    for (let i = 0; i < count; i++) {
      const o = i * EVENT_SIZE;
      out.push({
        timeUs: Number(view.getBigInt64(o + EventField.timeUs, true)),
        price: Number(view.getBigInt64(o + EventField.price, true)),
        value: Number(view.getBigInt64(o + EventField.value, true)),
        orderId: Number(view.getBigUint64(o + EventField.orderId, true)),
        qty: view.getInt32(o + EventField.qty, true),
        participant: view.getUint32(o + EventField.participant, true),
        type: view.getUint8(o + EventField.type) as EventTypeCode,
        side: view.getUint8(o + EventField.side) as SideCode,
        reason: view.getUint8(o + EventField.reason),
        flags: view.getUint8(o + EventField.flags),
        aux: view.getUint32(o + EventField.aux, true),
      });
    }
  }
}
