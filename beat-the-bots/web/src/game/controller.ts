// Owns one round: drives the engine from requestAnimationFrame, forwards
// player commands, and turns engine events into render-ready state (tape,
// candles, headlines, toasts). It never decides anything about the market;
// every rule lives in the engine.
import type { WasmStats } from '../engine/wasm/btb.js';
import {
  BtbEngine,
  type Catalog,
  type EngineEvent,
  type Frame,
  type GameOptions,
  type RoundSummary,
} from '../engine/engine';
import { CANCEL_FLAG_STOP, EventType, OrderType, PLAYER, Side, type SideCode } from '../engine/layout';
import { ticksToPoints } from '../engine/format';
import type { BtbModule } from '../engine/wasm/btb.js';
import { CandleBuilder } from './candles';

export interface TapeEntry {
  key: number;
  timeUs: number;
  price: number;
  qty: number;
  side: SideCode; // aggressor
  mine: boolean;
}

export interface NewsEntry {
  key: number;
  timeUs: number;
  text: string;
}

export interface FillMarker {
  timeUs: number;
  price: number;
  qty: number;
  side: SideCode;
}

export type ToastKind = 'fill' | 'info' | 'error' | 'danger';
export interface Toast {
  key: number;
  kind: ToastKind;
  text: string;
  at: number; // performance.now()
}

export interface TradeFlash {
  price: number;
  side: SideCode;
  at: number;
}

const TAPE_MAX = 80;
const NEWS_MAX = 30;
const TOAST_MS = 3200;
const MAX_FRAME_MS = 250;

export class GameController {
  readonly options: GameOptions;
  readonly catalog: Catalog;
  readonly candles: CandleBuilder;
  frame: Frame;
  tape: TapeEntry[] = [];
  news: NewsEntry[] = [];
  fills: FillMarker[] = [];
  toasts: Toast[] = [];
  flash: TradeFlash | null = null;
  marginAlertAt: number | null = null;
  lastNewsAt: number | null = null;
  stats: WasmStats | null = null;
  summary: RoundSummary | null = null;
  paused = false;
  frameMs = 0; // smoothed main-thread cost of step + refresh

  private readonly engine: BtbEngine;
  private listeners = new Set<() => void>();
  private version = 0;
  private raf = 0;
  private lastNow = 0;
  private lastStatsAt = 0;
  private keySeq = 0;
  private running = false;

  constructor(mod: BtbModule, catalog: Catalog, options: GameOptions) {
    this.options = options;
    this.catalog = catalog;
    this.engine = new BtbEngine(mod, options);
    const { frame } = this.engine.refresh();
    this.frame = frame;
    this.candles = new CandleBuilder(frame.mark);
    this.stats = this.engine.stats();
  }

  // ------------------------------------------------------------ lifecycle

  start(): void {
    if (this.running) return;
    this.running = true;
    this.lastNow = performance.now();
    const loop = (now: number) => {
      if (!this.running) return;
      this.tick(now);
      this.raf = requestAnimationFrame(loop);
    };
    this.raf = requestAnimationFrame(loop);
  }

  stop(): void {
    this.running = false;
    cancelAnimationFrame(this.raf);
  }

  dispose(): void {
    this.stop();
    this.engine.dispose();
    this.listeners.clear();
  }

  setPaused(p: boolean): void {
    this.paused = p;
    this.lastNow = performance.now();
    this.emit();
  }

  private tick(now: number): void {
    const dt = Math.min(MAX_FRAME_MS, now - this.lastNow);
    this.lastNow = now;
    if (!this.paused && !this.frame.finished) {
      const t0 = performance.now();
      this.engine.step(dt);
      this.pull(now);
      this.frameMs = this.frameMs * 0.95 + (performance.now() - t0) * 0.05;
    }
    if (now - this.lastStatsAt > 500) {
      this.stats = this.engine.stats();
      this.lastStatsAt = now;
    }
    this.toasts = this.toasts.filter((t) => now - t.at < TOAST_MS);
    if (this.frame.finished && !this.summary) {
      this.summary = this.engine.summary();
      this.stop();
    }
    this.emit();
  }

  /** Reads the engine snapshot and folds new events into UI state. */
  private pull(now = performance.now()): void {
    const { frame, events } = this.engine.refresh();
    this.frame = frame;
    this.ingest(events, now);
    this.candles.advanceTo(frame.timeUs);
  }

  // ------------------------------------------------------------ commands

  buyMarket(qty: number): void {
    this.command(() => this.engine.submit(Side.Buy, OrderType.Market, 0, qty));
  }
  sellMarket(qty: number): void {
    this.command(() => this.engine.submit(Side.Sell, OrderType.Market, 0, qty));
  }
  limit(side: SideCode, price: number, qty: number): void {
    this.command(() => this.engine.submit(side, OrderType.Limit, price, qty));
  }
  /** Stop-market at `trigger`: the engine fires it once the last trade gets there. */
  stopOrder(side: SideCode, trigger: number, qty: number): void {
    this.command(() => this.engine.submitStop(side, trigger, qty));
  }
  /** Cancels a resting order or a working stop. */
  cancel(id: number): void {
    this.command(() => this.engine.cancel(id));
  }
  cancelAll(): void {
    this.command(() => this.engine.cancelAll());
  }
  flatten(): void {
    this.command(() => this.engine.flatten());
  }

  // Commands apply immediately, so the next frame is read right away: acks
  // and fills show up in the same frame as the click.
  private command(fn: () => unknown): void {
    if (this.frame.finished || this.paused) return;
    fn();
    this.pull();
    this.emit();
  }

  // ------------------------------------------------------------ events

  private ingest(events: EngineEvent[], now: number): void {
    // Several fills of one order in one frame become one toast.
    const fillAgg = new Map<number, { side: SideCode; qty: number; notional: number }>();
    for (const e of events) {
      switch (e.type) {
        case EventType.Trade: {
          this.candles.addTrade(e.timeUs, e.price, e.qty);
          this.tape.unshift({
            key: ++this.keySeq,
            timeUs: e.timeUs,
            price: e.price,
            qty: e.qty,
            side: e.side,
            mine: e.participant === PLAYER || e.aux === PLAYER,
          });
          this.flash = { price: e.price, side: e.side, at: now };
          break;
        }
        case EventType.Fill: {
          if (e.participant !== PLAYER) break;
          this.fills.push({ timeUs: e.timeUs, price: e.price, qty: e.qty, side: e.side });
          const agg = fillAgg.get(e.orderId) ?? { side: e.side, qty: 0, notional: 0 };
          agg.qty += e.qty;
          agg.notional += e.qty * e.price;
          fillAgg.set(e.orderId, agg);
          break;
        }
        case EventType.Reject:
          if (e.participant === PLAYER)
            this.toast('error', `Rejected: ${this.catalog.rejectReasons[e.reason] ?? 'unknown reason'}`, now);
          break;
        case EventType.Cancel:
          // User cancels are visible on the ladder; only explain the others.
          if (e.participant === PLAYER && e.reason !== 0 && e.reason !== 2) {
            const what = e.flags & CANCEL_FLAG_STOP ? 'Stop' : 'Order';
            this.toast('info', `${what} cancelled: ${this.catalog.cancelReasons[e.reason] ?? ''}`, now);
          }
          break;
        case EventType.Trigger:
          if (e.participant === PLAYER)
            this.toast(
              'info',
              `Stop ${ticksToPoints(e.price)} hit: ${e.side === Side.Buy ? 'buying' : 'selling'} ${e.qty} at market`,
              now,
            );
          break;
        case EventType.MarginCall:
          if (e.participant === PLAYER) {
            this.marginAlertAt = now;
            this.toast('danger', 'MARGIN CALL: equity fell below maintenance margin', now);
          }
          break;
        case EventType.Liquidation:
          if (e.participant === PLAYER)
            this.toast('danger', `Liquidating ${Math.abs(e.qty)} contract${Math.abs(e.qty) === 1 ? '' : 's'} at market`, now);
          break;
        case EventType.News: {
          const h = this.catalog.headlines[e.aux];
          this.news.unshift({ key: ++this.keySeq, timeUs: e.timeUs, text: h ? h.text : '…' });
          this.lastNewsAt = now;
          break;
        }
        default:
          break;
      }
    }
    for (const f of fillAgg.values()) {
      const avg = Math.round(f.notional / f.qty);
      this.toast('fill', `${f.side === Side.Buy ? 'Bought' : 'Sold'} ${f.qty} @ ${ticksToPoints(avg)}`, now);
    }
    if (this.tape.length > TAPE_MAX) this.tape.length = TAPE_MAX;
    if (this.news.length > NEWS_MAX) this.news.length = NEWS_MAX;
  }

  private toast(kind: ToastKind, text: string, at: number): void {
    this.toasts = [...this.toasts.slice(-3), { key: ++this.keySeq, kind, text, at }];
  }

  // ------------------------------------------------------------ store

  subscribe = (fn: () => void): (() => void) => {
    this.listeners.add(fn);
    return () => this.listeners.delete(fn);
  };

  getVersion = (): number => this.version;

  private emit(): void {
    this.version++;
    for (const fn of this.listeners) fn();
  }
}
