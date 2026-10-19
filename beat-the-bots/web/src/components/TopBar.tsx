import { useState } from 'react';
import type { GameController } from '../game/controller';
import { useFrame, type Prefs } from '../game/hooks';
import { Phase } from '../engine/layout';
import { formatClock, ticksToPoints } from '../engine/format';
import { shareUrl } from '../game/url';

const PHASE_LABEL: Record<number, string> = {
  [Phase.Open]: 'Calm open',
  [Phase.Midday]: 'Midday',
  [Phase.Close]: 'Into the close: volatility rising',
  [Phase.Settlement]: 'Settlement window: final 10s VWAP',
  [Phase.Settled]: 'Settled',
};

interface Props {
  ctrl: GameController;
  prefs: Prefs;
  setPrefs: (p: Partial<Prefs>) => void;
  onQuit: () => void;
  goal?: string;
}

export function TopBar({ ctrl, prefs, setPrefs, onQuit, goal }: Props) {
  useFrame(ctrl);
  const [copied, setCopied] = useState(false);
  const f = ctrl.frame;
  const remaining = f.roundUs - f.timeUs;
  const urgent = remaining < 30_000_000;
  const { seed, difficulty, scenario } = ctrl.options;

  const copy = async () => {
    const url = shareUrl(window.location.href, seed, difficulty, scenario);
    try {
      await navigator.clipboard.writeText(url);
      setCopied(true);
      setTimeout(() => setCopied(false), 1500);
    } catch {
      window.prompt('Copy this link', url);
    }
  };

  return (
    <header className="flex flex-wrap items-center gap-x-4 gap-y-1 border-b border-line px-4 py-2">
      <div className="flex items-center gap-2">
        <svg width="20" height="20" viewBox="0 0 32 32" aria-hidden="true">
          <path d="M4 23 L12 14 L17 18 L28 7" fill="none" stroke="var(--buy)" strokeWidth="3.5" strokeLinecap="round" strokeLinejoin="round" />
        </svg>
        <span className="font-bold tracking-tight">Beat the Bots</span>
      </div>

      <div className="flex items-center gap-3">
        <span className={`num text-xl font-bold ${urgent ? 'text-danger' : ''}`} aria-label="Time remaining">
          {formatClock(remaining)}
        </span>
        <span className="hidden text-xs text-muted md:inline">{PHASE_LABEL[f.phase]}</span>
      </div>

      <div className="num hidden items-baseline gap-2 text-sm lg:flex">
        <span className="text-muted">BTB</span>
        <span className="font-semibold">{ticksToPoints(f.lastTrade ?? f.mark)}</span>
      </div>

      {goal && (
        <span className="hidden truncate rounded border border-accent/50 px-2 py-0.5 text-xs text-accent xl:inline">Goal: {goal}</span>
      )}

      <div className="ml-auto flex items-center gap-2 text-xs">
        <span className="rounded bg-panel-2 px-2 py-1 uppercase tracking-wider text-muted">{difficulty}</span>
        <button type="button" onClick={copy} className="num rounded bg-panel-2 px-2 py-1 text-muted hover:text-text" title="Copy a link to this exact market">
          seed {seed} {copied ? '✓ copied' : '⧉'}
        </button>
        <button
          type="button"
          onClick={() => setPrefs({ showStats: !prefs.showStats })}
          aria-pressed={prefs.showStats}
          className={`rounded px-2 py-1 ${prefs.showStats ? 'bg-text text-bg' : 'bg-panel-2 text-muted hover:text-text'}`}
        >
          Engine stats
        </button>
        <button
          type="button"
          onClick={() => setPrefs({ colorblind: !prefs.colorblind })}
          aria-pressed={prefs.colorblind}
          className={`rounded px-2 py-1 ${prefs.colorblind ? 'bg-text text-bg' : 'bg-panel-2 text-muted hover:text-text'}`}
          title="Colorblind-safe blue/orange palette"
        >
          CB colors
        </button>
        <button type="button" onClick={onQuit} className="rounded bg-panel-2 px-2 py-1 text-muted hover:text-text" aria-label="Quit to menu">
          Quit
        </button>
      </div>
    </header>
  );
}
