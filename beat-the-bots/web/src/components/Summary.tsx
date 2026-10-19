import { useEffect, useMemo, useRef, useState } from 'react';
import {
  BaselineSeries,
  ColorType,
  createChart,
  createSeriesMarkers,
  type SeriesMarker,
  type Time,
  type UTCTimestamp,
} from 'lightweight-charts';
import type { GameController } from '../game/controller';
import type { RoundSummary } from '../engine/engine';
import { Side } from '../engine/layout';
import { centsToDollars, pct, signedDollars, ticksToPoints } from '../engine/format';
import { cssVar } from '../game/hooks';
import { beatPct, loadLeaderboard, recordResult, type LeaderboardEntry } from '../game/leaderboard';
import { shareUrl } from '../game/url';

const BASE = 1_700_000_000;

interface Props {
  ctrl: GameController;
  summary: RoundSummary;
  goal?: string;
  onReplay: () => void;
  onNew: () => void;
  onMenu: () => void;
}

export function Summary({ ctrl, summary: s, goal, onReplay, onNew, onMenu }: Props) {
  const [board, setBoard] = useState<{ board: LeaderboardEntry[]; rank: number } | null>(null);
  const [copied, setCopied] = useState(false);
  const recorded = useRef(false);

  useEffect(() => {
    if (recorded.current) return;
    recorded.current = true;
    setBoard(recordResult(loadLeaderboard(), s));
  }, [s]);

  const winRate = s.closes ? Math.round((100 * s.wins) / s.closes) : null;
  const beat = beatPct(s);
  const byKind = useMemo(() => {
    const m = new Map<string, number[]>();
    for (const b of s.bots) m.set(b.kind, [...(m.get(b.kind) ?? []), b.pnl]);
    return [...m.entries()]
      .map(([kind, pnls]) => ({
        kind,
        count: pnls.length,
        avg: pnls.reduce((a, b) => a + b, 0) / pnls.length,
        beaten: pnls.filter((p) => p < s.pnl).length,
      }))
      .sort((a, b) => b.avg - a.avg);
  }, [s]);

  const link = shareUrl(window.location.href, s.seed, s.difficulty, s.scenario);
  const copy = async () => {
    try {
      await navigator.clipboard.writeText(link);
      setCopied(true);
    } catch {
      window.prompt('Copy this link', link);
    }
  };

  const verdict =
    s.liquidations > 0
      ? 'Liquidated.'
      : beat >= 90
        ? 'You beat the bots.'
        : s.pnl > 0
          ? 'In the green.'
          : s.pnl === 0
            ? 'Sat it out.'
            : 'The bots got you this time.';

  return (
    <div className="fixed inset-0 z-50 overflow-y-auto bg-bg/95 backdrop-blur-sm" role="dialog" aria-label="Round results">
      <div className="mx-auto max-w-5xl px-4 py-8">
        <div className="text-xs uppercase tracking-widest text-muted">
          Round over · seed {s.seed} · {s.difficulty}
          {s.scenario !== 'none' ? ` · ${s.scenario.replace('_', ' ')}` : ''}
        </div>
        <h1 className="mt-1 text-3xl font-black">{verdict}</h1>
        {goal && (
          <p className={`mt-1 text-sm ${s.goalMet ? 'text-buy' : 'text-sell'}`}>
            {s.goalMet ? '✓ Goal met' : '✗ Goal missed'}: {goal}
          </p>
        )}

        <div className="mt-6 grid grid-cols-2 gap-3 md:grid-cols-6">
          <Big label="Final P&L" value={signedDollars(s.pnl)} tone={s.pnl} />
          <Big label="Return" value={pct(s.returnBp)} tone={s.pnl} />
          <Big label="Max drawdown" value={centsToDollars(-s.maxDrawdown)} tone={s.maxDrawdown ? -1 : 0} />
          <Big label="Trades" value={String(s.fills)} />
          <Big label="Win rate" value={winRate == null ? '-' : `${winRate}%`} />
          <Big label="Bots beaten" value={`${beat}%`} tone={beat >= 50 ? 1 : -1} />
        </div>

        <div className="panel mt-4 p-3">
          <div className="mb-2 flex items-center justify-between">
            <span className="panel-title">Equity curve</span>
            <span className="num text-xs text-muted">
              settled at {ticksToPoints(s.settlementPrice)} · hidden fair value was {ticksToPoints(s.fairValue)}
            </span>
          </div>
          <EquityChart ctrl={ctrl} summary={s} />
        </div>

        <div className="mt-4 grid gap-4 md:grid-cols-2">
          <div className="panel p-3">
            <div className="panel-title mb-2">
              You beat {s.botsBeaten} of {s.botCount} bots
            </div>
            <table className="num w-full text-sm">
              <thead className="text-left text-[11px] uppercase tracking-wider text-muted">
                <tr>
                  <th className="py-1 font-normal">Bot type</th>
                  <th className="py-1 text-right font-normal">Avg P&L</th>
                  <th className="py-1 text-right font-normal">You beat</th>
                </tr>
              </thead>
              <tbody>
                {byKind.map((k) => (
                  <tr key={k.kind} className="border-t border-line">
                    <td className="py-1 font-sans capitalize">
                      {k.kind} <span className="text-muted">×{k.count}</span>
                    </td>
                    <td className={`py-1 text-right ${k.avg >= 0 ? 'text-buy' : 'text-sell'}`}>{signedDollars(k.avg, true)}</td>
                    <td className="py-1 text-right">
                      {k.beaten}/{k.count}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>

          <div className="panel p-3">
            <div className="panel-title mb-2">Your best rounds (this browser)</div>
            {board && board.board.length > 0 ? (
              <ol className="num text-sm">
                {board.board.map((e, idx) => (
                  <li
                    key={`${e.at}-${idx}`}
                    className={`flex justify-between border-t border-line py-1 ${idx + 1 === board.rank ? 'text-accent' : ''}`}
                  >
                    <span>
                      {idx + 1}. seed {e.seed} · {e.difficulty}
                    </span>
                    <span>{signedDollars(e.pnl, true)}</span>
                  </li>
                ))}
              </ol>
            ) : (
              <p className="text-sm text-muted">Results are saved locally when storage is available.</p>
            )}
          </div>
        </div>

        <div className="mt-6 flex flex-wrap items-center gap-3">
          <button type="button" onClick={onNew} className="rounded-md bg-buy px-5 py-2.5 font-bold text-bg" autoFocus>
            Play a new market
          </button>
          <button type="button" onClick={onReplay} className="rounded-md border border-line bg-panel-2 px-4 py-2.5 font-semibold">
            Replay this market
          </button>
          <button type="button" onClick={copy} className="rounded-md border border-line bg-panel-2 px-4 py-2.5 font-semibold">
            {copied ? '✓ Link copied' : 'Challenge a friend (copy link)'}
          </button>
          <button type="button" onClick={onMenu} className="px-2 py-2.5 text-sm text-muted hover:text-text">
            Menu
          </button>
        </div>
        <p className="mt-2 text-xs text-muted">Same seed, same market: anyone who opens the link faces exactly these bots and headlines.</p>
      </div>
    </div>
  );
}

function Big({ label, value, tone = 0 }: { label: string; value: string; tone?: number }) {
  const c = tone > 0 ? 'text-buy' : tone < 0 ? 'text-sell' : 'text-text';
  return (
    <div className="panel p-3">
      <div className="panel-title">{label}</div>
      <div className={`num mt-1 text-xl font-bold ${c}`}>{value}</div>
    </div>
  );
}

function EquityChart({ ctrl, summary }: { ctrl: GameController; summary: RoundSummary }) {
  const el = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const start = ctrl.frame.startingCash;
    const chart = createChart(el.current!, {
      autoSize: true,
      layout: {
        background: { type: ColorType.Solid, color: 'transparent' },
        textColor: cssVar('--muted'),
        fontFamily: 'ui-monospace, Menlo, monospace',
        fontSize: 11,
        attributionLogo: false,
      },
      grid: { vertLines: { visible: false }, horzLines: { color: cssVar('--line') } },
      rightPriceScale: { borderColor: cssVar('--line') },
      timeScale: {
        borderColor: cssVar('--line'),
        timeVisible: true,
        secondsVisible: true,
        tickMarkFormatter: (t: Time) => {
          const sec = (t as number) - BASE;
          return `${Math.floor(sec / 60)}:${String(sec % 60).padStart(2, '0')}`;
        },
      },
      localization: { priceFormatter: (p: number) => `$${Math.round(p).toLocaleString()}` },
      handleScroll: false,
      handleScale: false,
    });
    const series = chart.addSeries(BaselineSeries, {
      baseValue: { type: 'price', price: start / 100 },
      topLineColor: cssVar('--buy'),
      bottomLineColor: cssVar('--sell'),
      topFillColor1: cssVar('--buy-soft'),
      topFillColor2: 'transparent',
      bottomFillColor1: 'transparent',
      bottomFillColor2: cssVar('--sell-soft'),
      lineWidth: 2,
    });
    const data = summary.equity.map((v, i) => ({ time: (BASE + i) as UTCTimestamp, value: v / 100 }));
    series.setData(data);
    const last = data.length - 1;
    const markers: SeriesMarker<Time>[] = ctrl.fills.map((f) => ({
      time: (BASE + Math.min(last, Math.floor(f.timeUs / 1_000_000))) as UTCTimestamp,
      position: f.side === Side.Buy ? 'belowBar' : 'aboveBar',
      shape: f.side === Side.Buy ? 'arrowUp' : 'arrowDown',
      color: f.side === Side.Buy ? cssVar('--buy') : cssVar('--sell'),
    }));
    createSeriesMarkers(series, markers);
    chart.timeScale().fitContent();
    return () => chart.remove();
  }, [ctrl, summary]);
  return <div ref={el} className="h-56 w-full" />;
}
