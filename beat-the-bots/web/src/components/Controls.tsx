import type { GameController } from '../game/controller';
import { useFrame } from '../game/hooks';

interface Props {
  ctrl: GameController;
  size: number;
  onSize: (n: number) => void;
}

const SIZES = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];

function Key({ k }: { k: string }) {
  return <kbd className="ml-1.5 rounded border border-current/40 px-1 font-mono text-[10px] opacity-70">{k}</kbd>;
}

/** Order size and the four big buttons. Hotkeys are wired in GameScreen. */
export function Controls({ ctrl, size, onSize }: Props) {
  useFrame(ctrl);
  const f = ctrl.frame;
  const closed = f.finished || ctrl.paused || ctrl.replay;
  const working = f.orders.length + f.stops.length;

  return (
    <div className="panel flex flex-col gap-3 p-3">
      <div>
        <div className="mb-1.5 flex items-center justify-between">
          <span className="panel-title">Size (contracts)</span>
          <span className="text-[11px] text-muted">keys 1–9</span>
        </div>
        <div className="grid grid-cols-10 gap-1" role="radiogroup" aria-label="Order size">
          {SIZES.map((n) => (
            <button
              key={n}
              type="button"
              role="radio"
              aria-checked={size === n}
              onClick={() => onSize(n)}
              className={`num h-8 rounded text-[13px] ${
                size === n ? 'bg-text font-bold text-bg' : 'bg-panel-2 text-muted hover:text-text'
              }`}
            >
              {n}
            </button>
          ))}
        </div>
      </div>

      <div className="grid grid-cols-2 gap-2">
        <button
          type="button"
          data-coach="buy"
          disabled={closed}
          onClick={() => ctrl.buyMarket(size)}
          className="h-12 rounded-md bg-buy text-[15px] font-bold tracking-wide text-bg hover:brightness-110 active:brightness-95"
        >
          BUY MKT {size}
          <Key k="B" />
        </button>
        <button
          type="button"
          disabled={closed}
          onClick={() => ctrl.sellMarket(size)}
          className="h-12 rounded-md bg-sell text-[15px] font-bold tracking-wide text-bg hover:brightness-110 active:brightness-95"
        >
          SELL MKT {size}
          <Key k="S" />
        </button>
        <button
          type="button"
          disabled={closed || (f.position === 0 && working === 0)}
          onClick={() => ctrl.flatten()}
          className="h-10 whitespace-nowrap rounded-md border border-line bg-panel-2 text-[13px] font-semibold hover:border-muted"
        >
          FLATTEN
          <Key k="F" />
        </button>
        <button
          type="button"
          disabled={closed || working === 0}
          onClick={() => ctrl.cancelAll()}
          className="h-10 rounded-md border border-line bg-panel-2 text-[13px] font-semibold hover:border-muted"
        >
          CANCEL ALL
          <Key k="Esc" />
        </button>
      </div>
    </div>
  );
}
