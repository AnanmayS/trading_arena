import type { GameController } from '../game/controller';
import { useFrame } from '../game/hooks';

const TOAST_STYLE = {
  fill: 'border-accent/60 text-text',
  info: 'border-line text-muted',
  error: 'border-sell/60 text-sell',
  danger: 'border-danger bg-danger/20 text-text font-semibold',
} as const;

export function Toasts({ ctrl }: { ctrl: GameController }) {
  useFrame(ctrl);
  return (
    <div className="pointer-events-none fixed left-4 top-24 z-30 flex w-72 flex-col gap-2" aria-live="polite">
      {ctrl.toasts.map((t) => (
        <div key={t.key} className={`slide-down rounded-md border bg-panel-2 px-3 py-2 text-sm shadow-lg ${TOAST_STYLE[t.kind]}`}>
          {t.text}
        </div>
      ))}
    </div>
  );
}

/** Full-screen alarm when the account is margin-called. */
export function MarginCallOverlay({ ctrl }: { ctrl: GameController }) {
  useFrame(ctrl);
  const at = ctrl.marginAlertAt;
  if (at == null || performance.now() - at > 2800) return null;
  return (
    <div key={at} className="margin-call pointer-events-none fixed inset-0 z-40 flex items-center justify-center" role="alert">
      <div className="rounded-lg border-2 border-danger bg-bg/90 px-8 py-5 text-center shadow-2xl">
        <div className="text-3xl font-black tracking-widest text-danger">MARGIN CALL</div>
        <div className="mt-1 text-sm text-text">Equity fell below maintenance. Your position is being liquidated at market.</div>
      </div>
    </div>
  );
}

export function StatsPanel({ ctrl }: { ctrl: GameController }) {
  useFrame(ctrl);
  const s = ctrl.stats;
  if (!s) return null;
  const rows: [string, string][] = [
    ['Orders processed', s.orders.toLocaleString()],
    ['Trades', s.trades.toLocaleString()],
    ['Events / sec', s.eventsPerSec.toLocaleString()],
    ['Engine ns / order', s.avgNsPerOrder.toLocaleString()],
    ['Sim ticks', s.ticks.toLocaleString()],
    ['Frame cost (JS+WASM)', `${ctrl.frameMs.toFixed(2)} ms`],
  ];
  return (
    <div className="panel num fixed bottom-4 right-4 z-30 w-64 p-3 text-xs shadow-xl" aria-label="Engine statistics">
      <div className="panel-title mb-2">C++ engine (WebAssembly)</div>
      {rows.map(([k, v]) => (
        <div key={k} className="flex justify-between py-0.5">
          <span className="text-muted">{k}</span>
          <span>{v}</span>
        </div>
      ))}
    </div>
  );
}
