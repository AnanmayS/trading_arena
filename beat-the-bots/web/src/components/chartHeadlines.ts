// Headline markers shared by the in-game candle chart and the summary charts:
// a marker on the second each headline was released, and a tooltip with the
// headline text while the crosshair is on that second.
import type { IChartApi, MouseEventParams, SeriesMarker, Time, UTCTimestamp } from 'lightweight-charts';
import type { HeadlineMark } from '../game/controller';

// lightweight-charts wants epoch seconds; round seconds are offset onto a
// fixed base and formatted back to mm:ss.
export const BASE = 1_700_000_000;
export const toTime = (sec: number) => (BASE + sec) as UTCTimestamp;
export const toSec = (t: Time) => Math.max(0, (t as number) - BASE);
export const clock = (t: Time) => {
  const s = toSec(t);
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
};

export const headlineSec = (h: HeadlineMark) => Math.max(0, Math.floor(h.timeUs / 1_000_000));

/** One marker per headline. `colorOf` lets the end screen colour by sentiment. */
export function headlineMarkers(
  headlines: readonly HeadlineMark[],
  colorOf: (h: HeadlineMark) => string,
  lastSec = Infinity,
): SeriesMarker<Time>[] {
  return headlines.map((h) => ({
    time: toTime(Math.min(lastSec, headlineSec(h))),
    position: 'aboveBar',
    shape: 'square',
    color: colorOf(h),
    size: 0.7,
    text: 'N',
  }));
}

/** Markers must be in time order; fills and headlines are merged here. */
export function sortMarkers(ms: SeriesMarker<Time>[]): SeriesMarker<Time>[] {
  return ms.sort((a, b) => (a.time as number) - (b.time as number));
}

/**
 * Shows the headline text when the crosshair sits on a second with a headline.
 * `host` must be positioned (relative) so the tooltip can sit inside it.
 * Returns an unsubscribe function.
 */
export function attachHeadlineTooltip(
  chart: IChartApi,
  host: HTMLElement,
  getHeadlines: () => readonly HeadlineMark[],
  lastSec = Infinity,
): () => void {
  const tip = document.createElement('div');
  tip.setAttribute('role', 'tooltip');
  tip.className =
    'pointer-events-none absolute z-10 hidden max-w-[260px] rounded border border-accent bg-panel-2 px-2 py-1 text-[12px] leading-snug whitespace-pre-line text-text shadow-lg';
  host.appendChild(tip);

  const onMove = (p: MouseEventParams<Time>) => {
    if (p.time === undefined || !p.point) {
      tip.classList.add('hidden');
      return;
    }
    const sec = toSec(p.time);
    const hits = getHeadlines().filter((h) => Math.min(lastSec, headlineSec(h)) === sec);
    if (hits.length === 0) {
      tip.classList.add('hidden');
      return;
    }
    tip.textContent = hits.map((h) => h.text).join('\n');
    tip.classList.remove('hidden');
    const x = Math.min(Math.max(0, p.point.x - 20), Math.max(0, host.clientWidth - tip.offsetWidth - 4));
    tip.style.left = `${x}px`;
    tip.style.top = '6px';
  };
  chart.subscribeCrosshairMove(onMove);
  return () => {
    chart.unsubscribeCrosshairMove(onMove);
    tip.remove();
  };
}
