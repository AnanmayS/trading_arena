import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { Controls } from '../src/components/Controls';
import { Landing } from '../src/components/Landing';
import { PositionPanel } from '../src/components/PositionPanel';
import type { GameController } from '../src/game/controller';
import type { Frame } from '../src/engine/engine';

afterEach(cleanup);

const frame = (over: Partial<Frame> = {}): Frame => ({
  timeUs: 0,
  roundUs: 300_000_000,
  phase: 0,
  finished: false,
  bestBid: 19999,
  bestAsk: 20001,
  lastTrade: 20000,
  mark: 20000,
  position: 0,
  openCost: 0,
  realized: 0,
  unrealized: 0,
  equity: 2_500_000,
  initialMargin: 0,
  maintMargin: 0,
  ticksToLiq: -1,
  openBuy: 0,
  openSell: 0,
  liquidating: false,
  fees: 0,
  fills: 0,
  totalPnl: 0,
  maxDrawdown: 0,
  startingCash: 2_500_000,
  tickValue: 1250,
  ticksPerPoint: 4,
  maxPosition: 10,
  priceBand: 200,
  liquidations: 0,
  bids: [],
  asks: [],
  orders: [],
  ...over,
});

// Just enough of GameController for presentational components.
function fakeCtrl(f: Frame) {
  const listeners = new Set<() => void>();
  let version = 0;
  const ctrl = {
    frame: f,
    paused: false,
    subscribe: (fn: () => void) => {
      listeners.add(fn);
      return () => listeners.delete(fn);
    },
    getVersion: () => version,
    buyMarket: vi.fn(),
    sellMarket: vi.fn(),
    flatten: vi.fn(),
    cancelAll: vi.fn(),
    push(next: Frame) {
      ctrl.frame = next;
      version++;
      listeners.forEach((l) => l());
    },
  };
  return ctrl;
}

describe('PositionPanel', () => {
  it('shows a flat account with no liquidation risk', () => {
    const c = fakeCtrl(frame());
    render(<PositionPanel ctrl={c as unknown as GameController} />);
    expect(screen.getByText('FLAT')).toBeTruthy();
    expect(screen.getByText('flat: no liquidation risk')).toBeTruthy();
    expect(screen.getByRole('meter').getAttribute('aria-valuenow')).toBe('0');
  });

  it('updates every frame: long position, P&L and distance to liquidation', () => {
    const c = fakeCtrl(frame());
    render(<PositionPanel ctrl={c as unknown as GameController} />);
    act(() =>
      c.push(
        frame({
          position: 4,
          openCost: 80_000,
          unrealized: 50_000,
          totalPnl: 49_600,
          equity: 2_100_000,
          initialMargin: 2_400_000,
          maintMargin: 2_000_000,
          ticksToLiq: 20,
        }),
      ),
    );
    expect(screen.getByText('+4 LONG')).toBeTruthy();
    expect(screen.getByText('+$496.00')).toBeTruthy();
    expect(screen.getByText(/20 ticks \(5.00 pts\) to liquidation/)).toBeTruthy();
    expect(Number(screen.getByRole('meter').getAttribute('aria-valuenow'))).toBe(95);
  });
});

describe('Controls', () => {
  it('sends market orders with the selected size and disables idle buttons', () => {
    const c = fakeCtrl(frame());
    const onSize = vi.fn();
    render(<Controls ctrl={c as unknown as GameController} size={3} onSize={onSize} />);
    fireEvent.click(screen.getByRole('button', { name: /BUY MKT 3/ }));
    fireEvent.click(screen.getByRole('button', { name: /SELL MKT 3/ }));
    expect(c.buyMarket).toHaveBeenCalledWith(3);
    expect(c.sellMarket).toHaveBeenCalledWith(3);
    fireEvent.click(screen.getByRole('radio', { name: '7' }));
    expect(onSize).toHaveBeenCalledWith(7);
    expect((screen.getByRole('button', { name: /FLATTEN/ }) as HTMLButtonElement).disabled).toBe(true);
    expect((screen.getByRole('button', { name: /CANCEL ALL/ }) as HTMLButtonElement).disabled).toBe(true);
  });

  it('locks trading once the round is over', () => {
    const c = fakeCtrl(frame({ finished: true }));
    render(<Controls ctrl={c as unknown as GameController} size={1} onSize={() => {}} />);
    expect((screen.getByRole('button', { name: /BUY MKT/ }) as HTMLButtonElement).disabled).toBe(true);
  });
});

describe('Landing', () => {
  const catalog = {
    headlines: [],
    scenarios: [{ key: 'flash_crash', title: 'Flash Crash', goal: 'Survive.', seed: 1987, difficulty: 'normal' as const }],
    rejectReasons: [],
    cancelReasons: [],
  };

  it('waits for the engine, then starts a round with the chosen difficulty', () => {
    const onPlay = vi.fn();
    const { rerender } = render(
      <Landing ready={false} catalog={null} challenge={null} roundSeconds={null} tutorialDone={false} onPlay={onPlay} />,
    );
    expect((screen.getByRole('button', { name: /Loading engine/ }) as HTMLButtonElement).disabled).toBe(true);
    rerender(<Landing ready catalog={catalog} challenge={null} roundSeconds={null} tutorialDone={false} onPlay={onPlay} />);
    fireEvent.click(screen.getByRole('radio', { name: 'Hard' }));
    fireEvent.click(screen.getByRole('button', { name: 'PLAY' }));
    expect(onPlay).toHaveBeenCalledWith(expect.objectContaining({ difficulty: 'hard' }), true);
  });

  it('plays a shared challenge seed and launches scenarios with their preset', () => {
    const onPlay = vi.fn();
    render(
      <Landing
        ready
        catalog={catalog}
        challenge={{ seed: 555, difficulty: 'easy', scenario: null }}
        roundSeconds={null}
        tutorialDone
        onPlay={onPlay}
      />,
    );
    expect(screen.getByText(/Challenge accepted/)).toBeTruthy();
    fireEvent.click(screen.getByRole('button', { name: 'PLAY' }));
    expect(onPlay).toHaveBeenLastCalledWith(expect.objectContaining({ seed: 555, difficulty: 'easy' }), false);
    fireEvent.click(screen.getByRole('button', { name: /Flash Crash/ }));
    expect(onPlay).toHaveBeenLastCalledWith(
      expect.objectContaining({ seed: 1987, scenario: 'flash_crash', difficulty: 'normal' }),
      false,
    );
  });
});
