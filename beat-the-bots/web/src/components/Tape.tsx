import type { GameController } from '../game/controller';
import { useFrame } from '../game/hooks';
import { Side } from '../engine/layout';
import { ticksToPoints } from '../engine/format';

function stamp(us: number) {
  const s = us / 1_000_000;
  const m = Math.floor(s / 60);
  return `${m}:${(s - m * 60).toFixed(1).padStart(4, '0')}`;
}

/** Time & sales, coloured by aggressor side; your own trades are tagged. */
export function Tape({ ctrl, rows = 40 }: { ctrl: GameController; rows?: number }) {
  useFrame(ctrl);
  const tape = ctrl.tape.slice(0, rows);
  return (
    <div className="panel flex h-full min-h-0 flex-col">
      <div className="flex items-center justify-between border-b border-line px-3 py-2">
        <span className="panel-title">Time &amp; sales</span>
        <span className="text-[11px] text-muted">color = aggressor</span>
      </div>
      <div className="grid grid-cols-[1fr_1fr_0.6fr] px-3 py-1 text-[10px] uppercase tracking-wider text-muted">
        <span>Time</span>
        <span className="text-right">Price</span>
        <span className="text-right">Size</span>
      </div>
      <ul className="num min-h-0 flex-1 overflow-hidden text-[12px]" aria-live="off">
        {tape.map((t) => (
          <li
            key={t.key}
            className={`grid grid-cols-[1fr_1fr_0.6fr] px-3 leading-[19px] ${t.side === Side.Buy ? 'text-buy' : 'text-sell'} ${
              t.mine ? 'bg-[rgb(243_186_47/0.12)]' : ''
            }`}
          >
            <span className="text-muted">
              {stamp(t.timeUs)}
              {t.mine && <span className="ml-1 rounded-sm bg-accent px-1 text-[9px] font-bold text-bg">YOU</span>}
            </span>
            <span className="text-right">{ticksToPoints(t.price)}</span>
            <span className="text-right">{t.qty}</span>
          </li>
        ))}
      </ul>
    </div>
  );
}
