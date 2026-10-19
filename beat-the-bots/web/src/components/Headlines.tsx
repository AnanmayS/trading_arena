import type { GameController } from '../game/controller';
import { useFrame } from '../game/hooks';
import { formatClock } from '../engine/format';

/** Headlines feed. Market-moving news lands a little before the price jump. */
export function Headlines({ ctrl }: { ctrl: GameController }) {
  useFrame(ctrl);
  return (
    <div className="panel flex h-full min-h-0 flex-col" data-coach="news">
      <div className="flex items-center justify-between border-b border-line px-3 py-2">
        <span className="panel-title">Headlines</span>
        <span className="text-[11px] text-muted">read fast</span>
      </div>
      <ul className="min-h-0 flex-1 overflow-y-auto text-[13px]">
        {ctrl.news.length === 0 && <li className="px-3 py-3 text-muted">Quiet so far. Headlines will appear here.</li>}
        {ctrl.news.map((n, i) => (
          <li key={n.key} className={`border-b border-line px-3 py-2 ${i === 0 ? 'flash-in' : ''}`}>
            <span className="num mr-2 text-[11px] text-muted">{formatClock(n.timeUs)}</span>
            <span className={i === 0 ? 'text-text' : 'text-muted'}>{n.text}</span>
          </li>
        ))}
      </ul>
    </div>
  );
}

/** Banner that slides in over the top of the screen for a fresh headline. */
export function BreakingNews({ ctrl }: { ctrl: GameController }) {
  useFrame(ctrl);
  const latest = ctrl.news[0];
  if (!latest || ctrl.lastNewsAt == null || performance.now() - ctrl.lastNewsAt > 6000) return null;
  return (
    <div className="pointer-events-none fixed inset-x-0 top-0 z-30 flex justify-center px-4 pt-2" role="status">
      <div key={latest.key} className="slide-down flex max-w-2xl items-center gap-3 rounded-md border border-accent bg-panel-2 px-4 py-2 shadow-lg">
        <span className="rounded-sm bg-accent px-1.5 py-0.5 text-[10px] font-bold tracking-wider text-bg">BREAKING</span>
        <span className="text-sm font-medium">{latest.text}</span>
      </div>
    </div>
  );
}
