import { useState } from 'react';
import type { Catalog, Difficulty, GameOptions } from '../engine/engine';
import { signedDollars } from '../engine/format';
import { loadLeaderboard } from '../game/leaderboard';
import { randomSeed } from '../game/url';

interface Props {
  ready: boolean;
  catalog: Catalog | null;
  challenge: { seed: number; difficulty: Difficulty; scenario: string | null } | null;
  roundSeconds: number | null;
  onPlay: (opts: GameOptions, tutorial: boolean) => void;
  tutorialDone: boolean;
}

const DIFFS: { key: Difficulty; label: string; hint: string }[] = [
  { key: 'easy', label: 'Easy', hint: 'calm market, headlines arrive ~2s early' },
  { key: 'normal', label: 'Normal', hint: 'real bots, ~1s head start on news' },
  { key: 'hard', label: 'Hard', hint: 'wide spreads, sharp insiders, no warning' },
];

export function Landing({ ready, catalog, challenge, roundSeconds, onPlay, tutorialDone }: Props) {
  const [difficulty, setDifficulty] = useState<Difficulty>(challenge?.difficulty ?? 'normal');
  const board = loadLeaderboard().slice(0, 5);

  const play = (opts: Partial<GameOptions> = {}, tutorial = !tutorialDone) =>
    onPlay(
      {
        seed: challenge?.seed ?? randomSeed(),
        difficulty,
        scenario: challenge?.scenario ?? undefined,
        roundSeconds: roundSeconds ?? undefined,
        ...opts,
      },
      tutorial,
    );

  return (
    <main className="mx-auto flex min-h-full max-w-5xl flex-col px-4 py-10">
      <div className="flex items-center gap-3">
        <svg width="36" height="36" viewBox="0 0 32 32" aria-hidden="true">
          <rect width="32" height="32" rx="6" fill="var(--panel-2)" />
          <path d="M5 22 L12 14 L17 18 L27 8" fill="none" stroke="var(--buy)" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round" />
        </svg>
        <h1 className="text-4xl font-black tracking-tight">Beat the Bots</h1>
      </div>
      <p className="mt-3 max-w-2xl text-lg text-muted">
        Trade a futures contract against 32 trading bots for five minutes. Read the headlines, work the order book, and
        don&apos;t get liquidated.
      </p>

      {challenge && (
        <div className="mt-6 rounded-md border border-accent/60 bg-panel px-4 py-3 text-sm">
          <span className="font-semibold text-accent">Challenge accepted:</span> you&apos;ll trade seed{' '}
          <span className="num">{challenge.seed}</span> ({challenge.difficulty}
          {challenge.scenario ? `, ${challenge.scenario.replace('_', ' ')}` : ''}), the exact market your friend played.
        </div>
      )}

      <section className="mt-8 flex flex-col gap-4 md:flex-row md:items-end">
        <button
          type="button"
          disabled={!ready}
          onClick={() => play()}
          className="h-16 rounded-lg bg-buy px-12 text-2xl font-black tracking-wide text-bg shadow-lg hover:brightness-110 disabled:opacity-50"
        >
          {ready ? 'PLAY' : 'Loading engine…'}
        </button>
        <div>
          <div className="panel-title mb-1.5">Difficulty</div>
          <div className="flex gap-1 rounded-lg bg-panel p-1" role="radiogroup" aria-label="Difficulty">
            {DIFFS.map((d) => (
              <button
                key={d.key}
                type="button"
                role="radio"
                aria-checked={difficulty === d.key}
                onClick={() => setDifficulty(d.key)}
                className={`rounded-md px-4 py-2 text-sm font-semibold ${
                  difficulty === d.key ? 'bg-text text-bg' : 'text-muted hover:text-text'
                }`}
                title={d.hint}
              >
                {d.label}
              </button>
            ))}
          </div>
          <div className="mt-1 text-xs text-muted">{DIFFS.find((d) => d.key === difficulty)?.hint}</div>
        </div>
        {tutorialDone && (
          <button type="button" onClick={() => play({}, true)} className="text-sm text-muted underline-offset-4 hover:text-text hover:underline md:ml-auto">
            How it works (30s tutorial)
          </button>
        )}
      </section>

      <section className="mt-12">
        <h2 className="panel-title mb-3">Scenarios</h2>
        <div className="grid gap-3 sm:grid-cols-2 lg:grid-cols-4">
          {(catalog?.scenarios ?? []).map((sc) => (
            <button
              key={sc.key}
              type="button"
              disabled={!ready}
              onClick={() => play({ seed: sc.seed, difficulty: sc.difficulty, scenario: sc.key })}
              className="panel group p-4 text-left hover:border-muted"
            >
              <div className="font-bold group-hover:text-accent">{sc.title}</div>
              <div className="mt-1 text-sm text-muted">{sc.goal}</div>
              <div className="num mt-3 text-[11px] uppercase tracking-wider text-faint">
                seed {sc.seed} · {sc.difficulty}
              </div>
            </button>
          ))}
        </div>
      </section>

      <section className="mt-12 grid gap-8 md:grid-cols-2">
        <div>
          <h2 className="panel-title mb-3">How it works</h2>
          <ul className="space-y-2 text-sm text-muted">
            <li>
              <span className="text-text">Buy low, sell high.</span> Hit BUY MKT (B) or SELL MKT (S), or click the ladder to post
              limit orders.
            </li>
            <li>
              <span className="text-text">Headlines move the market.</span> They land a moment before the price jumps. React
              faster than the bots.
            </li>
            <li>
              <span className="text-text">$25,000 account, $6,000 margin per contract.</span> Lose too much and you get
              liquidated.
            </li>
            <li>
              <span className="text-text">At 5:00 the contract settles</span> at the average price of the last 10 seconds.
            </li>
          </ul>
        </div>
        <div>
          <h2 className="panel-title mb-3">Your best rounds</h2>
          {board.length ? (
            <ol className="num space-y-1 text-sm">
              {board.map((e, i) => (
                <li key={`${e.at}-${i}`} className="flex justify-between border-b border-line pb-1">
                  <span className="text-muted">
                    {i + 1}. seed {e.seed} · {e.difficulty}
                  </span>
                  <span className={e.pnl >= 0 ? 'text-buy' : 'text-sell'}>{signedDollars(e.pnl, true)}</span>
                </li>
              ))}
            </ol>
          ) : (
            <p className="text-sm text-muted">No rounds yet. Your results are saved in this browser.</p>
          )}
        </div>
      </section>

      <footer className="mt-auto pt-12 text-xs text-faint">
        C++20 matching engine compiled to WebAssembly · integer ticks, zero-sum accounting, deterministic seeds ·{' '}
        <a className="underline hover:text-muted" href="https://github.com/AnanmayS/trading_arena">
          source
        </a>
      </footer>
    </main>
  );
}
