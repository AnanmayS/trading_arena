import type { GameController } from '../game/controller';
import { useFrame } from '../game/hooks';
import { centsToDollars, signedDollars, ticksToPoints } from '../engine/format';

function pnlClass(c: number) {
  return c > 0 ? 'text-buy' : c < 0 ? 'text-sell' : 'text-text';
}

function Stat({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="min-w-0">
      <div className="panel-title truncate">{label}</div>
      <div className="num truncate text-[15px] leading-6">{children}</div>
    </div>
  );
}

/** Position, P&L, equity and how close the account is to liquidation. */
export function PositionPanel({ ctrl }: { ctrl: GameController }) {
  useFrame(ctrl);
  const f = ctrl.frame;
  const pos = f.position;
  const avg = pos !== 0 ? ticksToPoints(f.openCost / pos) : '-';
  // Maintenance usage: 0% when flat, 100% means liquidation.
  const usage = pos === 0 ? 0 : Math.min(1, Math.max(0, f.maintMargin / Math.max(1, f.equity)));
  const barColor = usage < 0.5 ? 'bg-buy' : usage < 0.8 ? 'bg-accent' : 'bg-danger';

  return (
    <div className="panel flex h-full flex-col justify-between gap-3 p-3">
      <div className="flex items-end justify-between gap-4">
        <div data-coach="pnl">
          <div className="panel-title">Round P&amp;L</div>
          <div className={`num text-3xl font-bold leading-9 ${pnlClass(f.totalPnl)}`}>{signedDollars(f.totalPnl)}</div>
        </div>
        <div className="num text-right text-[11px] text-muted">
          <div>fees paid {centsToDollars(f.fees)}</div>
          <div>
            {f.fills} fill{f.fills === 1 ? '' : 's'} · {f.orders.length} working
          </div>
        </div>
      </div>
      <div className="grid grid-cols-3 gap-x-4 gap-y-2 sm:grid-cols-6">
        <Stat label="Position">
          <span className={pos > 0 ? 'text-buy' : pos < 0 ? 'text-sell' : 'text-muted'}>
            {pos > 0 ? `+${pos} LONG` : pos < 0 ? `${pos} SHORT` : 'FLAT'}
          </span>
        </Stat>
        <Stat label="Avg price">{avg}</Stat>
        <Stat label="Unrealized">
          <span className={pnlClass(f.unrealized)}>{signedDollars(f.unrealized)}</span>
        </Stat>
        <Stat label="Realized">
          <span className={pnlClass(f.realized)}>{signedDollars(f.realized)}</span>
        </Stat>
        <Stat label="Equity">{centsToDollars(f.equity)}</Stat>
        <Stat label="Margin used">
          <span title={`${centsToDollars(f.initialMargin, true)} initial margin`}>
            {pos === 0 ? '0%' : `${Math.round((100 * f.initialMargin) / Math.max(1, f.equity))}%`}
          </span>
          <span className="ml-1 text-[11px] text-muted">{pos === 0 ? '' : centsToDollars(f.initialMargin, true)}</span>
        </Stat>
      </div>

      <div data-coach="margin">
        <div className="mb-1 flex items-center justify-between text-[11px]">
          <span className="panel-title">Maintenance margin</span>
          <span className={`num ${usage >= 0.8 ? 'text-danger' : 'text-muted'}`}>
            {f.liquidating
              ? 'LIQUIDATING'
              : pos === 0
                ? 'flat: no liquidation risk'
                : `${f.ticksToLiq} ticks (${ticksToPoints(f.ticksToLiq)} pts) to liquidation`}
          </span>
        </div>
        <div
          className="h-2 overflow-hidden rounded-full bg-panel-2"
          role="meter"
          aria-label="Maintenance margin usage"
          aria-valuemin={0}
          aria-valuemax={100}
          aria-valuenow={Math.round(usage * 100)}
        >
          <div className={`h-full ${barColor} transition-[width] duration-200`} style={{ width: `${usage * 100}%` }} />
        </div>
      </div>
    </div>
  );
}
