import { useEffect, useRef } from 'react';
import type { GameController } from '../game/controller';
import { cssVar } from '../game/hooks';
import { Side } from '../engine/layout';
import { ticksToPoints } from '../engine/format';

// Price ladder (DOM) drawn on a canvas: one draw per engine frame, no React
// re-render. Columns: my bids | bid size | price | ask size | my asks.
// Click the bid side to buy at that price, the ask side to sell, and your own
// working order to cancel it.

const ROW_H = 22;
const COLS = [0.16, 0.22, 0.24, 0.22, 0.16];
type Col = 0 | 1 | 2 | 3 | 4; // myBid, bid, price, ask, myAsk

interface Props {
  ctrl: GameController;
  size: number;
}

interface Palette {
  bg: string;
  panel2: string;
  line: string;
  text: string;
  muted: string;
  faint: string;
  buy: string;
  sell: string;
  buySoft: string;
  sellSoft: string;
  accent: string;
  danger: string;
}

function readPalette(): Palette {
  return {
    bg: cssVar('--panel'),
    panel2: cssVar('--panel-2'),
    line: cssVar('--line'),
    text: cssVar('--text'),
    muted: cssVar('--muted'),
    faint: cssVar('--faint'),
    buy: cssVar('--buy'),
    sell: cssVar('--sell'),
    buySoft: cssVar('--buy-soft'),
    sellSoft: cssVar('--sell-soft'),
    accent: cssVar('--accent'),
    danger: cssVar('--danger'),
  };
}

export function Ladder({ ctrl, size }: Props) {
  const wrapRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const sizeRef = useRef(size);
  sizeRef.current = size;
  const ui = useRef({
    center: null as number | null,
    manualUntil: 0,
    hoverRow: -1,
    hoverCol: -1 as number,
    width: 0,
    height: 0,
    rows: 0,
    palette: null as Palette | null,
    paletteKey: '',
  });

  useEffect(() => {
    const canvas = canvasRef.current!;
    const wrap = wrapRef.current!;
    const ctx = canvas.getContext('2d')!;
    const s = ui.current;

    const colX = (w: number) => {
      const xs = [0];
      for (const c of COLS) xs.push(xs[xs.length - 1] + c * w);
      return xs;
    };

    const topPrice = () => (s.center ?? ctrl.frame.mark) + Math.floor(s.rows / 2);

    const draw = () => {
      const key = document.documentElement.dataset.palette ?? '';
      if (!s.palette || s.paletteKey !== key) {
        s.palette = readPalette();
        s.paletteKey = key;
      }
      const p = s.palette;
      const f = ctrl.frame;
      const now = performance.now();
      const w = s.width;
      const h = s.height;
      if (!w || !h) return;

      // Keep the market in view: re-centre when the mid drifts near the edge,
      // unless the user scrolled the ladder recently.
      const mid = f.mark;
      if (s.center == null) s.center = mid;
      if (now > s.manualUntil && Math.abs(mid - s.center) > s.rows / 2 - 4) s.center = mid;

      ctx.fillStyle = p.bg;
      ctx.fillRect(0, 0, w, h);

      const xs = colX(w);
      const top = topPrice();
      const bids = new Map(f.bids.map((l) => [l.price, l.qty]));
      const asks = new Map(f.asks.map((l) => [l.price, l.qty]));
      let maxQty = 1;
      for (const l of f.bids) maxQty = Math.max(maxQty, l.qty);
      for (const l of f.asks) maxQty = Math.max(maxQty, l.qty);
      const mine = new Map<string, number>();
      for (const o of f.orders) {
        const k = `${o.side}:${o.price}`;
        mine.set(k, (mine.get(k) ?? 0) + o.qty);
      }
      const avg = f.position !== 0 ? Math.round(f.openCost / f.position) : null;
      const liq = f.position !== 0 && f.ticksToLiq >= 0 ? f.mark - Math.sign(f.position) * f.ticksToLiq : null;
      const flash = ctrl.flash && now - ctrl.flash.at < 450 ? ctrl.flash : null;

      ctx.textBaseline = 'middle';
      ctx.font = `12px ${cssVar('--font-mono') || 'monospace'}`;

      for (let r = 0; r < s.rows; r++) {
        const price = top - r;
        const y = r * ROW_H;
        const cy = y + ROW_H / 2;
        const isBestBid = price === f.bestBid;
        const isBestAsk = price === f.bestAsk;

        if (r % 2 === 1) {
          ctx.fillStyle = p.panel2;
          ctx.fillRect(0, y, w, ROW_H);
        }
        if (flash && flash.price === price) {
          ctx.globalAlpha = 1 - (now - flash.at) / 450;
          ctx.fillStyle = flash.side === Side.Buy ? p.buySoft : p.sellSoft;
          ctx.fillRect(0, y, w, ROW_H);
          ctx.globalAlpha = 1;
        }
        if (r === s.hoverRow) {
          ctx.fillStyle = 'rgba(255,255,255,0.05)';
          ctx.fillRect(0, y, w, ROW_H);
        }

        // Size bars and sizes.
        const bq = bids.get(price);
        if (bq) {
          const bw = (xs[2] - xs[1]) * Math.min(1, bq / maxQty);
          ctx.fillStyle = p.buySoft;
          ctx.fillRect(xs[2] - bw, y + 2, bw, ROW_H - 4);
          ctx.fillStyle = p.buy;
          ctx.font = `${isBestBid ? 'bold ' : ''}12px ui-monospace, Menlo, monospace`;
          ctx.textAlign = 'right';
          ctx.fillText(String(bq), xs[2] - 6, cy);
        }
        const aq = asks.get(price);
        if (aq) {
          const aw = (xs[4] - xs[3]) * Math.min(1, aq / maxQty);
          ctx.fillStyle = p.sellSoft;
          ctx.fillRect(xs[3], y + 2, aw, ROW_H - 4);
          ctx.fillStyle = p.sell;
          ctx.font = `${isBestAsk ? 'bold ' : ''}12px ui-monospace, Menlo, monospace`;
          ctx.textAlign = 'left';
          ctx.fillText(String(aq), xs[3] + 6, cy);
        }

        // Price column.
        const isLast = price === f.lastTrade;
        if (isLast) {
          ctx.strokeStyle = p.accent;
          ctx.lineWidth = 1;
          ctx.strokeRect(xs[2] + 2.5, y + 2.5, xs[3] - xs[2] - 5, ROW_H - 5);
        }
        ctx.font = `${isLast || isBestBid || isBestAsk ? 'bold ' : ''}12px ui-monospace, Menlo, monospace`;
        ctx.textAlign = 'center';
        ctx.fillStyle = isLast ? p.accent : bq || aq ? p.text : p.muted;
        ctx.fillText(ticksToPoints(price), (xs[2] + xs[3]) / 2, cy);

        // Average entry (accent wedge on the price cell) and liquidation markers.
        if (avg === price) {
          ctx.fillStyle = p.accent;
          ctx.beginPath();
          ctx.moveTo(xs[2] + 1, cy - 5);
          ctx.lineTo(xs[2] + 7, cy);
          ctx.lineTo(xs[2] + 1, cy + 5);
          ctx.closePath();
          ctx.fill();
        }
        if (liq === price) {
          ctx.strokeStyle = p.danger;
          ctx.setLineDash([4, 3]);
          ctx.beginPath();
          ctx.moveTo(0, y + 0.5);
          ctx.lineTo(w, y + 0.5);
          ctx.stroke();
          ctx.setLineDash([]);
          ctx.fillStyle = p.danger;
          ctx.font = 'bold 9px ui-sans-serif, system-ui, sans-serif';
          ctx.textAlign = 'right';
          ctx.fillText('LIQ', xs[3] - 4, cy);
        }

        // Working orders and hover hints.
        const myBid = mine.get(`${Side.Buy}:${price}`);
        const myAsk = mine.get(`${Side.Sell}:${price}`);
        drawChip(ctx, xs[0], xs[1], cy, myBid, p.buy, r === s.hoverRow && s.hoverCol === 0);
        drawChip(ctx, xs[4], xs[5], cy, myAsk, p.sell, r === s.hoverRow && s.hoverCol === 4);
        if (r === s.hoverRow && !f.finished) {
          ctx.font = '11px ui-sans-serif, system-ui, sans-serif';
          ctx.textAlign = 'center';
          if ((s.hoverCol === 0 && !myBid) || s.hoverCol === 1) {
            ctx.fillStyle = p.buy;
            ctx.fillText(`BUY ${sizeRef.current}`, (xs[0] + xs[1]) / 2, cy);
          }
          if ((s.hoverCol === 4 && !myAsk) || s.hoverCol === 3) {
            ctx.fillStyle = p.sell;
            ctx.fillText(`SELL ${sizeRef.current}`, (xs[4] + xs[5]) / 2, cy);
          }
        }
      }

      // Column separators.
      ctx.strokeStyle = p.line;
      ctx.lineWidth = 1;
      for (const x of [xs[1], xs[2], xs[3], xs[4]]) {
        ctx.beginPath();
        ctx.moveTo(Math.round(x) + 0.5, 0);
        ctx.lineTo(Math.round(x) + 0.5, h);
        ctx.stroke();
      }
    };

    const resize = () => {
      const rect = wrap.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      s.width = rect.width;
      s.height = rect.height;
      s.rows = Math.max(5, Math.floor(rect.height / ROW_H));
      canvas.width = Math.round(rect.width * dpr);
      canvas.height = Math.round(rect.height * dpr);
      canvas.style.width = `${rect.width}px`;
      canvas.style.height = `${rect.height}px`;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      draw();
    };

    const locate = (ev: PointerEvent | MouseEvent) => {
      const rect = canvas.getBoundingClientRect();
      const x = ev.clientX - rect.left;
      const y = ev.clientY - rect.top;
      const xs = colX(s.width);
      let col = 4;
      for (let i = 0; i < 5; i++) if (x < xs[i + 1]) {
        col = i;
        break;
      }
      return { row: Math.floor(y / ROW_H), col: col as Col };
    };

    const onMove = (ev: PointerEvent) => {
      const { row, col } = locate(ev);
      s.hoverRow = row;
      s.hoverCol = col;
      canvas.style.cursor = col === 2 ? 'default' : 'pointer';
      draw();
    };
    const onLeave = () => {
      s.hoverRow = -1;
      draw();
    };
    const onClick = (ev: MouseEvent) => {
      const { row, col } = locate(ev);
      const price = topPrice() - row;
      const f = ctrl.frame;
      if (col === 0 || col === 4) {
        const side = col === 0 ? Side.Buy : Side.Sell;
        const own = f.orders.filter((o) => o.side === side && o.price === price);
        if (own.length) {
          for (const o of own) ctrl.cancel(o.id);
          return;
        }
      }
      if (col === 0 || col === 1) ctrl.limit(Side.Buy, price, sizeRef.current);
      else if (col === 3 || col === 4) ctrl.limit(Side.Sell, price, sizeRef.current);
    };
    const onWheel = (ev: WheelEvent) => {
      ev.preventDefault();
      s.center = (s.center ?? ctrl.frame.mark) + (ev.deltaY > 0 ? -2 : 2);
      s.manualUntil = performance.now() + 6000;
      draw();
    };

    const ro = new ResizeObserver(resize);
    ro.observe(wrap);
    const unsub = ctrl.subscribe(draw);
    canvas.addEventListener('pointermove', onMove);
    canvas.addEventListener('pointerleave', onLeave);
    canvas.addEventListener('click', onClick);
    canvas.addEventListener('wheel', onWheel, { passive: false });
    resize();
    return () => {
      ro.disconnect();
      unsub();
      canvas.removeEventListener('pointermove', onMove);
      canvas.removeEventListener('pointerleave', onLeave);
      canvas.removeEventListener('click', onClick);
      canvas.removeEventListener('wheel', onWheel);
    };
  }, [ctrl]);

  const recenter = () => {
    ui.current.center = null;
    ui.current.manualUntil = 0;
  };

  return (
    <div className="panel flex h-full min-h-0 flex-col" data-coach="ladder">
      <div className="flex items-center justify-between border-b border-line px-3 py-2">
        <span className="panel-title">Order book</span>
        <button type="button" onClick={recenter} className="text-[11px] text-muted hover:text-text" title="Re-center on the market">
          Re-center
        </button>
      </div>
      <div className="grid grid-cols-[16fr_22fr_24fr_22fr_16fr] border-b border-line px-0 py-1 text-center text-[10px] uppercase tracking-wider text-muted">
        <span>My bids</span>
        <span className="text-buy">Bids</span>
        <span>Price</span>
        <span className="text-sell">Asks</span>
        <span>My asks</span>
      </div>
      <div ref={wrapRef} className="relative min-h-0 flex-1 overflow-hidden" aria-label="Price ladder: click the bid side to buy, the ask side to sell">
        <canvas ref={canvasRef} className="absolute inset-0 touch-none select-none" />
      </div>
    </div>
  );
}

function drawChip(
  ctx: CanvasRenderingContext2D,
  x0: number,
  x1: number,
  cy: number,
  qty: number | undefined,
  color: string,
  hover: boolean,
) {
  if (!qty) return;
  const w = x1 - x0 - 10;
  ctx.fillStyle = color;
  ctx.globalAlpha = hover ? 0.4 : 0.22;
  ctx.fillRect(x0 + 5, cy - 8, w, 16);
  ctx.globalAlpha = 1;
  ctx.strokeStyle = color;
  ctx.strokeRect(x0 + 5.5, cy - 7.5, w - 1, 15);
  ctx.fillStyle = color;
  ctx.font = 'bold 11px ui-monospace, Menlo, monospace';
  ctx.textAlign = 'center';
  ctx.fillText(hover ? '✕' : String(qty), (x0 + x1) / 2, cy);
}
