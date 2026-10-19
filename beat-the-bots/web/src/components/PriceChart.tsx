import { useEffect, useRef } from 'react';
import {
  CandlestickSeries,
  ColorType,
  CrosshairMode,
  LineStyle,
  createChart,
  createSeriesMarkers,
  type IPriceLine,
  type ISeriesApi,
  type SeriesMarker,
  type Time,
  type UTCTimestamp,
} from 'lightweight-charts';
import type { GameController } from '../game/controller';
import { cssVar } from '../game/hooks';
import { Side } from '../engine/layout';

// 1-second candles with the player's fills as markers, the average entry price,
// the liquidation price and each working stop as horizontal lines. Updates are incremental: only
// the newest candle is pushed each frame.

// lightweight-charts wants epoch seconds; round seconds are offset onto a
// fixed base and formatted back to mm:ss.
const BASE = 1_700_000_000;
const toTime = (sec: number) => (BASE + sec) as UTCTimestamp;
const clock = (t: Time) => {
  const s = Math.max(0, (t as number) - BASE);
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
};

export function PriceChart({ ctrl, palette }: { ctrl: GameController; palette: string }) {
  const el = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const tpp = ctrl.frame.ticksPerPoint;
    const colors = { buy: cssVar('--buy'), sell: cssVar('--sell'), accent: cssVar('--accent'), danger: cssVar('--danger') };
    const chart = createChart(el.current!, {
      autoSize: true,
      layout: {
        background: { type: ColorType.Solid, color: 'transparent' },
        textColor: cssVar('--muted'),
        fontFamily: 'ui-monospace, Menlo, monospace',
        fontSize: 11,
        attributionLogo: false,
      },
      grid: { vertLines: { color: cssVar('--line') }, horzLines: { color: cssVar('--line') } },
      rightPriceScale: { borderColor: cssVar('--line') },
      timeScale: {
        borderColor: cssVar('--line'),
        timeVisible: true,
        secondsVisible: true,
        rightOffset: 4,
        barSpacing: 7,
        tickMarkFormatter: (t: Time) => clock(t),
      },
      localization: { timeFormatter: (t: Time) => clock(t), priceFormatter: (p: number) => p.toFixed(2) },
      crosshair: { mode: CrosshairMode.Normal },
    });
    const series: ISeriesApi<'Candlestick'> = chart.addSeries(CandlestickSeries, {
      upColor: colors.buy,
      downColor: colors.sell,
      wickUpColor: colors.buy,
      wickDownColor: colors.sell,
      borderVisible: false,
      priceFormat: { type: 'price', precision: 2, minMove: 1 / tpp },
    });
    const markers = createSeriesMarkers(series, []);
    let avgLine: IPriceLine | null = null;
    let liqLine: IPriceLine | null = null;
    const stopLines = new Map<number, IPriceLine>();
    let sent = 0;
    let fillCount = 0;

    const toBar = (c: (typeof ctrl.candles.candles)[number]) => ({
      time: toTime(c.time),
      open: c.open / tpp,
      high: c.high / tpp,
      low: c.low / tpp,
      close: c.close / tpp,
    });

    const setLine = (line: IPriceLine | null, price: number | null, color: string, title: string) => {
      if (price == null) {
        if (line) series.removePriceLine(line);
        return null;
      }
      if (line) {
        line.applyOptions({ price });
        return line;
      }
      return series.createPriceLine({ price, color, lineWidth: 1, lineStyle: LineStyle.Dashed, axisLabelVisible: true, title });
    };

    const update = () => {
      const cs = ctrl.candles.candles;
      if (sent === 0 && cs.length > 1) {
        series.setData(cs.map(toBar));
        sent = cs.length;
      } else {
        for (let i = Math.max(0, sent - 1); i < cs.length; i++) series.update(toBar(cs[i]));
        sent = cs.length;
      }

      if (ctrl.fills.length !== fillCount) {
        fillCount = ctrl.fills.length;
        const ms: SeriesMarker<Time>[] = ctrl.fills.map((f) => ({
          time: toTime(Math.floor(f.timeUs / 1_000_000)),
          position: f.side === Side.Buy ? 'belowBar' : 'aboveBar',
          shape: f.side === Side.Buy ? 'arrowUp' : 'arrowDown',
          color: f.side === Side.Buy ? colors.buy : colors.sell,
          text: `${f.side === Side.Buy ? 'B' : 'S'}${f.qty}`,
        }));
        markers.setMarkers(ms);
      }

      const f = ctrl.frame;
      const avg = f.position !== 0 ? f.openCost / f.position / tpp : null;
      const liq = f.position !== 0 && f.ticksToLiq >= 0 ? (f.mark - Math.sign(f.position) * f.ticksToLiq) / tpp : null;
      avgLine = setLine(avgLine, avg, colors.accent, 'AVG');
      liqLine = setLine(liqLine, liq, colors.danger, 'LIQ');

      const live = new Set<number>();
      for (const st of f.stops) {
        live.add(st.id);
        if (stopLines.has(st.id)) continue;
        stopLines.set(
          st.id,
          series.createPriceLine({
            price: st.price / tpp,
            color: st.side === Side.Buy ? colors.buy : colors.sell,
            lineWidth: 1,
            lineStyle: LineStyle.Dotted,
            axisLabelVisible: true,
            title: `${st.side === Side.Buy ? 'BUY' : 'SELL'} STP ${st.qty}`,
          }),
        );
      }
      for (const [id, line] of stopLines) {
        if (live.has(id)) continue;
        series.removePriceLine(line);
        stopLines.delete(id);
      }
    };

    update();
    const unsub = ctrl.subscribe(update);
    return () => {
      unsub();
      chart.remove();
    };
  }, [ctrl, palette]);

  return (
    <div className="panel flex h-full min-h-0 flex-col">
      <div className="flex items-center justify-between border-b border-line px-3 py-2">
        <span className="panel-title">BTB · 1s candles</span>
        <span className="text-[11px] text-muted">▲▼ your fills · AVG entry · LIQ liquidation · STP stops</span>
      </div>
      <div ref={el} className="min-h-0 flex-1" />
    </div>
  );
}
