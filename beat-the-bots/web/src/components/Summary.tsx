import { useEffect, useMemo, useRef, useState } from 'react';
import {
  BaselineSeries,
  ColorType,
  LineSeries,
  LineStyle,
  createChart,
  createSeriesMarkers,
  type DeepPartial,
  type ChartOptions,
  type SeriesMarker,
  type Time,
} from 'lightweight-charts';
import type { GameController, HeadlineMark } from '../game/controller';
import type { RoundSummary } from '../engine/engine';
import { Side } from '../engine/layout';
import { centsToDollars, pct, signedDollars, ticksToPoints } from '../engine/format';
import { cssVar } from '../game/hooks';
import { botResults, fillEdge } from '../game/insights';
import { beatPct, loadLeaderboard, recordResult, type LeaderboardEntry } from '../game/leaderboard';
import { shareUrl } from '../game/url';
import { attachHeadlineTooltip, clock, headlineMarkers, sortMarkers, toTime } from './chartHeadlines';

interface Props {
  ctrl: GameController;
  summary: RoundSummary;
  goal?: string;
  /** Set when this summary closes a replay: the P&L of the round it re-played. */
  replayOf?: number;
  onWatchReplay: () => void;
  onReplay: () => void;
  onNew: () => void;
  onMenu: () => void;
}

export function Summary({ ctrl, summary: s, goal, replayOf, onWatchReplay, onReplay, onNew, onMenu }: Props) {
  const [board, setBoard] = useState<{ board: LeaderboardEntry[]; rank: number } | null>(null);
  const [copied, setCopied] = useState(false);
  const recorded = useRef(false);
  const isReplay = replayOf !== undefined;

  useEffect(() => {
    if (recorded.current) return;
    recorded.current = true;
    // A replay is the same round again: show the board, do not record twice.
    setBoard(isReplay ? { board: loadLeaderboard(), rank: 0 } : recordResult(loadLeaderboard(), s));
  }, [s, isReplay]);

  const winRate = s.closes ? Math.round((100 * s.wins) / s.closes) : null;
  const beat = beatPct(s);
  const byKind = useMemo(() => botResults(s), [s]);
  const typesBeaten = byKind.filter((k) => k.beatAvg).length;
  const edge = useMemo(() => fillEdge(ctrl.fills, s.fairValues), [ctrl, s]);
  const tpp = ctrl.frame.ticksPerPoint;

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
        <h1 className="mt-1 text-3xl font-black">{isReplay ? `Replay finished. ${verdict}` : verdict}</h1>
        {isReplay && (
          <p className={`mt-1 text-sm ${replayOf === s.pnl ? 'text-buy' : 'text-sell'}`} data-testid="replay-check">
            {replayOf === s.pnl
              ? `✓ Replay reproduced your P&L exactly (${signedDollars(s.pnl)})`
              : `✗ Replay P&L ${signedDollars(s.pnl)} differs from the round (${signedDollars(replayOf)})`}
          </p>
        )}
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
          <Big
            label="Bot types beaten"
            value={`${typesBeaten} / ${byKind.length}`}
            tone={typesBeaten * 2 >= byKind.length ? 1 : -1}
          />
        </div>

        <div className="panel mt-4 p-3">
          <div className="mb-2 flex items-center justify-between">
            <span className="panel-title">Equity curve</span>
            <span className="text-[11px] text-muted">▲▼ your fills · N headline (hover for text)</span>
          </div>
          <EquityChart ctrl={ctrl} summary={s} />
        </div>

        <div className="panel mt-4 p-3">
          <div className="mb-2 flex flex-wrap items-center justify-between gap-x-4">
            <span className="panel-title">Price vs. the hidden fair value</span>
            <span className="num flex items-center gap-3 text-[11px] text-muted">
              <span>
                <span className="mr-1 inline-block h-0.5 w-4 bg-text align-middle" />
                traded price
              </span>
              <span>
                <span className="mr-1 inline-block w-4 border-t-2 border-dashed border-accent align-middle" />
                fair value
              </span>
              <span>
                settled {ticksToPoints(s.settlementPrice, tpp)} · fair value {ticksToPoints(s.fairValue, tpp)}
              </span>
            </span>
          </div>
          <FairValueChart ctrl={ctrl} summary={s} />
          <p className="mt-2 text-sm text-muted" data-testid="edge">
            {edge
              ? `${edge.goodContracts} of your ${edge.contracts} contracts traded on the right side of fair value (bought below it or sold above it). Average edge ${edge.avgEdgeTicks >= 0 ? '+' : ''}${(edge.avgEdgeTicks / tpp).toFixed(2)} points per contract${edge.avgEdgeTicks < 0 ? ': you paid up to trade with the bots that knew better.' : '.'}`
              : 'You did not trade. The dashed line is where the price was headed all along: trade when the price strays from it.'}{' '}
            The price chases fair value: headlines move it, and the Insider's orders drag the price toward it.
          </p>
        </div>

        <div className="mt-4 grid gap-4 md:grid-cols-2">
          <div className="panel p-3">
            <div className="panel-title mb-2">
              You beat {s.botsBeaten} of {s.botCount} bots ({beat}%)
            </div>
            <p className="mb-2 text-xs text-muted">Each type is judged on its own average per bot, against your {signedDollars(s.pnl, true)}.</p>
            <table className="num w-full text-sm" aria-label="Results by bot type">
              <thead className="text-left text-[11px] uppercase tracking-wider text-muted">
                <tr>
                  <th className="py-1 font-normal">Bot type</th>
                  <th className="py-1 text-right font-normal">Avg per bot</th>
                  <th className="py-1 text-right font-normal">You beat</th>
                </tr>
              </thead>
              <tbody>
                {byKind.map((k) => (
                  <tr key={k.kind} className="border-t border-line align-top">
                    <td className="py-1.5 pr-2 font-sans">
                      <span className={k.beatAvg ? 'text-buy' : 'text-sell'} aria-hidden="true">
                        {k.beatAvg ? '✓ ' : '✗ '}
                      </span>
                      <span className="font-semibold">{k.label}</span> <span className="text-muted">×{k.count}</span>
                      <div className="text-[11px] leading-snug text-muted">{k.blurb}</div>
                    </td>
                    <td className={`py-1.5 text-right ${k.avg >= 0 ? 'text-buy' : 'text-sell'}`}>{signedDollars(k.avg, true)}</td>
                    <td className="py-1.5 text-right">
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
          <button type="button" onClick={onWatchReplay} className="rounded-md border border-accent bg-panel-2 px-4 py-2.5 font-semibold text-accent">
            {isReplay ? 'Watch again' : 'Watch replay'}
          </button>
          <button type="button" onClick={onReplay} className="rounded-md border border-line bg-panel-2 px-4 py-2.5 font-semibold">
            Play this market again
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

const sentimentColor = (h: HeadlineMark) => cssVar(h.sentiment > 0 ? '--buy' : h.sentiment < 0 ? '--sell' : '--muted');

function chartOptions(priceFormatter: (p: number) => string): DeepPartial<ChartOptions> {
  return {
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
      tickMarkFormatter: (t: Time) => clock(t),
    },
    localization: { timeFormatter: (t: Time) => clock(t), priceFormatter },
    handleScroll: false,
    handleScale: false,
  };
}

function fillMarkers(ctrl: GameController, lastSec: number): SeriesMarker<Time>[] {
  return ctrl.fills.map((f) => ({
    time: toTime(Math.min(lastSec, Math.floor(f.timeUs / 1_000_000))),
    position: f.side === Side.Buy ? 'belowBar' : 'aboveBar',
    shape: f.side === Side.Buy ? 'arrowUp' : 'arrowDown',
    color: f.side === Side.Buy ? cssVar('--buy') : cssVar('--sell'),
  }));
}

function EquityChart({ ctrl, summary }: { ctrl: GameController; summary: RoundSummary }) {
  const el = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const start = ctrl.frame.startingCash;
    const chart = createChart(el.current!, chartOptions((p) => `$${Math.round(p).toLocaleString()}`));
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
    const data = summary.equity.map((v, i) => ({ time: toTime(i), value: v / 100 }));
    series.setData(data);
    const last = data.length - 1;
    createSeriesMarkers(
      series,
      sortMarkers([...fillMarkers(ctrl, last), ...headlineMarkers(ctrl.headlines, sentimentColor, last)]),
    );
    const untip = attachHeadlineTooltip(chart, el.current!, () => ctrl.headlines, last);
    chart.timeScale().fitContent();
    return () => {
      untip();
      chart.remove();
    };
  }, [ctrl, summary]);
  return <div ref={el} className="relative h-56 w-full" data-testid="equity-chart" />;
}

/** The answer key: traded price per second against the hidden fair value. */
function FairValueChart({ ctrl, summary }: { ctrl: GameController; summary: RoundSummary }) {
  const el = useRef<HTMLDivElement>(null);
  useEffect(() => {
    const tpp = ctrl.frame.ticksPerPoint;
    const chart = createChart(el.current!, chartOptions((p) => p.toFixed(2)));
    const fv = chart.addSeries(LineSeries, {
      color: cssVar('--accent'),
      lineWidth: 2,
      lineStyle: LineStyle.Dashed,
      priceLineVisible: false,
      crosshairMarkerVisible: false,
    });
    const price = chart.addSeries(LineSeries, {
      color: cssVar('--text'),
      lineWidth: 2,
      priceLineVisible: false,
    });
    fv.setData(summary.fairValues.map((v, i) => ({ time: toTime(i), value: v / tpp })));
    const closes = ctrl.candles.candles.filter((c) => c.time < summary.fairValues.length);
    price.setData(closes.map((c) => ({ time: toTime(c.time), value: c.close / tpp })));
    const last = Math.max(0, summary.fairValues.length - 1);
    createSeriesMarkers(
      price,
      sortMarkers([...fillMarkers(ctrl, last), ...headlineMarkers(ctrl.headlines, sentimentColor, last)]),
    );
    const untip = attachHeadlineTooltip(chart, el.current!, () => ctrl.headlines, last);
    chart.timeScale().fitContent();
    return () => {
      untip();
      chart.remove();
    };
  }, [ctrl, summary]);
  return <div ref={el} className="relative h-64 w-full" data-testid="fair-value-chart" />;
}
