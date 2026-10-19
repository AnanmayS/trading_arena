import { useEffect, useLayoutEffect, useState } from 'react';
import type { GameController } from '../game/controller';
import { useFrame } from '../game/hooks';

interface Step {
  target: string;
  title: string;
  body: string;
  // Advances on its own once this is true (the player did the thing).
  done?: (ctrl: GameController) => boolean;
  cta?: string;
}

const STEPS: Step[] = [
  {
    target: 'ladder',
    title: 'This is the order book',
    body: 'Bots post bids (green) and asks (red). Click the bid side of a row to buy at that price, the ask side to sell. Click your own order to cancel it.',
    cta: 'Next',
  },
  {
    target: 'buy',
    title: 'Buy one now',
    body: 'BUY MKT takes the best ask right away. Press it, or hit B. (S sells, F flattens.)',
    done: (c) => c.frame.fills > 0,
  },
  {
    target: 'pnl',
    title: 'Watch your P&L',
    body: 'Profit and loss updates every tick. Headlines move prices: read them fast and trade before the bots catch up.',
    cta: 'Next',
  },
  {
    target: 'margin',
    title: "Don't get liquidated",
    body: 'If losses eat into your margin this bar turns red, and at 100% the exchange closes your position for you. Beat the bots before the clock runs out.',
    cta: 'Start trading',
  },
];

interface Props {
  ctrl: GameController;
  onDone: () => void;
}

/** First-visit coach marks: ladder, buy, P&L, margin bar. ~30 seconds. */
export function Tutorial({ ctrl, onDone }: Props) {
  useFrame(ctrl);
  const [i, setI] = useState(0);
  const [rect, setRect] = useState<DOMRect | null>(null);
  const step = STEPS[i];

  // The market waits while you read the first card; it runs from "buy one" on.
  useEffect(() => {
    ctrl.setPaused(i === 0);
  }, [ctrl, i]);

  useLayoutEffect(() => {
    const el = document.querySelector(`[data-coach="${step.target}"]`);
    const measure = () => setRect(el ? el.getBoundingClientRect() : null);
    measure();
    window.addEventListener('resize', measure);
    return () => window.removeEventListener('resize', measure);
  }, [step.target]);

  const next = () => (i + 1 < STEPS.length ? setI(i + 1) : finish());
  const finish = () => {
    ctrl.setPaused(false);
    onDone();
  };

  useEffect(() => {
    if (step.done?.(ctrl)) next();
  });

  // Place the card below the target, else above, else beside it (tall
  // targets like the ladder), always clamped inside the viewport.
  const CARD_W = 340;
  const CARD_H = 190;
  const card: React.CSSProperties = { position: 'fixed', zIndex: 50, width: CARD_W, maxWidth: 'calc(100vw - 24px)' };
  if (rect) {
    const vw = window.innerWidth;
    const vh = window.innerHeight;
    const centeredLeft = Math.max(12, Math.min(vw - CARD_W - 12, rect.left + rect.width / 2 - CARD_W / 2));
    if (rect.bottom + CARD_H + 12 < vh) {
      card.left = centeredLeft;
      card.top = rect.bottom + 12;
    } else if (rect.top - CARD_H - 12 > 0) {
      card.left = centeredLeft;
      card.top = rect.top - CARD_H - 12;
    } else {
      const top = Math.max(12, Math.min(vh - CARD_H - 12, rect.top + 40));
      card.top = top;
      if (rect.left - CARD_W - 16 > 0) card.left = rect.left - CARD_W - 16;
      else if (rect.right + CARD_W + 16 < vw) card.left = rect.right + 16;
      else card.left = centeredLeft;
    }
  } else {
    card.left = 12;
    card.bottom = 12;
  }

  return (
    <>
      {rect && (
        <div
          aria-hidden="true"
          className="pointer-events-none fixed z-40 rounded-md border-2 border-accent"
          style={{
            left: rect.left - 4,
            top: rect.top - 4,
            width: rect.width + 8,
            height: rect.height + 8,
            boxShadow: '0 0 0 9999px rgb(0 0 0 / 0.55)',
          }}
        />
      )}
      <div style={card} role="dialog" aria-label="Tutorial" className="rounded-lg border border-accent bg-panel-2 p-4 shadow-2xl">
        <div className="mb-1 flex items-center justify-between">
          <span className="text-[11px] uppercase tracking-wider text-accent">
            How it works · {i + 1}/{STEPS.length}
          </span>
          <button type="button" onClick={finish} className="text-xs text-muted hover:text-text">
            Skip
          </button>
        </div>
        <h2 className="text-base font-bold">{step.title}</h2>
        <p className="mt-1 text-sm text-muted">{step.body}</p>
        {step.cta && (
          <button type="button" onClick={next} className="mt-3 rounded bg-accent px-3 py-1.5 text-sm font-bold text-bg">
            {step.cta}
          </button>
        )}
      </div>
    </>
  );
}
