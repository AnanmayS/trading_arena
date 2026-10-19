// Hand-written types for the Emscripten module produced by scripts/build_wasm.sh.

export interface WasmStats {
  orders: number;
  trades: number;
  events: number;
  eventsPerSec: number;
  avgNsPerOrder: number;
  ticks: number;
}

export interface WasmEngine {
  configError(): string;
  step(ms: number): void;
  submit(side: number, type: number, priceTicks: number, qty: number): number;
  cancel(id: number): boolean;
  cancelAll(): number;
  flatten(): void;
  refresh(): number;
  framePtr(): number;
  frameSize(): number;
  eventsPtr(): number;
  eventCount(): number;
  finished(): boolean;
  getStats(): WasmStats;
  summaryJson(): string;
  delete(): void;
}

export interface BtbModule {
  Engine: {
    new (seed: number, configJson: string): WasmEngine;
    catalog(): string;
    selfTest(seed: number, configJson: string): string;
  };
  HEAPU8: Uint8Array;
}

export default function createBtbModule(options?: Record<string, unknown>): Promise<BtbModule>;
