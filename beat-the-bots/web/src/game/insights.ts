// End-of-round explanations: per-archetype bot results and how the player's
// fills compared with the hidden fair value. Pure functions over the summary
// the engine reports once the round is finished.
import type { RoundSummary } from '../engine/engine';
import { Side } from '../engine/layout';
import type { FillMarker } from './controller';

interface BotInfo {
  label: string;
  blurb: string;
}

// Engine kind strings -> what the player sees. Display order is fixed so the
// Insider's outsized P&L never pushes the rest of the table around.
const BOT_INFO: Record<string, BotInfo> = {
  'market maker': { label: 'Market makers', blurb: 'Quote both sides and earn the spread; pull quotes on big news.' },
  momentum: { label: 'Momentum', blurb: 'Chase moves with market orders and bail when they stall.' },
  'mean reversion': { label: 'Mean reversion', blurb: 'Fade stretches away from the recent average.' },
  noise: { label: 'Noise', blurb: 'Random small orders: the liquidity everyone else feeds on.' },
  informed: {
    label: 'Insider',
    blurb: 'Sees a noisy copy of the hidden fair value and trades toward it, with far more size than you.',
  },
};
const ORDER = Object.keys(BOT_INFO);

export function botLabel(kind: string): string {
  return BOT_INFO[kind]?.label ?? kind;
}

export interface BotTypeResult {
  kind: string;
  label: string;
  blurb: string;
  count: number;
  avg: number; // cents per bot
  best: number; // cents
  beaten: number; // bots of this type the player out-earned
  beatAvg: boolean; // player P&L above this type's average
}

/** One row per bot archetype, each judged on its own average. */
export function botResults(s: Pick<RoundSummary, 'bots' | 'pnl'>): BotTypeResult[] {
  const by = new Map<string, number[]>();
  for (const b of s.bots) by.set(b.kind, [...(by.get(b.kind) ?? []), b.pnl]);
  const rank = (k: string) => (ORDER.includes(k) ? ORDER.indexOf(k) : ORDER.length);
  return [...by.entries()]
    .sort(([a], [b]) => rank(a) - rank(b) || a.localeCompare(b))
    .map(([kind, pnls]) => {
      const avg = pnls.reduce((a, b) => a + b, 0) / pnls.length;
      return {
        kind,
        label: botLabel(kind),
        blurb: BOT_INFO[kind]?.blurb ?? '',
        count: pnls.length,
        avg,
        best: Math.max(...pnls),
        beaten: pnls.filter((p) => p < s.pnl).length,
        beatAvg: s.pnl > avg,
      };
    });
}

export interface FillEdge {
  contracts: number;
  goodContracts: number; // bought below / sold above fair value
  avgEdgeTicks: number; // per contract, positive = you traded on the right side of fair value
}

/**
 * How each fill compared with the hidden fair value at that second. Buying
 * below fair value (or selling above it) is positive edge.
 */
export function fillEdge(fills: readonly FillMarker[], fairValues: readonly number[]): FillEdge | null {
  if (fills.length === 0 || fairValues.length === 0) return null;
  let contracts = 0;
  let good = 0;
  let edge = 0;
  for (const f of fills) {
    const sec = Math.min(fairValues.length - 1, Math.max(0, Math.floor(f.timeUs / 1_000_000)));
    const e = (fairValues[sec] - f.price) * (f.side === Side.Buy ? 1 : -1);
    contracts += f.qty;
    edge += e * f.qty;
    if (e > 0) good += f.qty;
  }
  return { contracts, goodContracts: good, avgEdgeTicks: edge / contracts };
}
