import { useEffect, useState } from 'react';
import type { BtbModule } from '../engine/wasm/btb.js';
import type { Catalog, GameOptions } from '../engine/engine';
import { GameController } from '../game/controller';
import { useFrame, useMediaQuery, usePrefs } from '../game/hooks';
import { Controls } from './Controls';
import { BreakingNews, Headlines } from './Headlines';
import { Ladder } from './Ladder';
import { MarginCallOverlay, StatsPanel, Toasts } from './Overlays';
import { PositionPanel } from './PositionPanel';
import { PriceChart } from './PriceChart';
import { Summary } from './Summary';
import { Tape } from './Tape';
import { TopBar } from './TopBar';
import { Tutorial } from './Tutorial';

interface Props {
  mod: BtbModule;
  catalog: Catalog;
  options: GameOptions;
  tutorial: boolean;
  onTutorialDone: () => void;
  onReplay: () => void;
  onNew: () => void;
  onQuit: () => void;
}

type Tab = 'ladder' | 'chart' | 'tape' | 'news';

export function GameScreen(props: Props) {
  const [ctrl, setCtrl] = useState<GameController | null>(null);
  const { mod, catalog, options } = props;

  // The controller owns WASM memory, so it is created and disposed with the
  // effect (React StrictMode mounts twice in development).
  useEffect(() => {
    const c = new GameController(mod, catalog, options);
    setCtrl(c);
    c.start();
    return () => c.dispose();
  }, [mod, catalog, options]);

  if (!ctrl) return null;
  return <Game ctrl={ctrl} {...props} />;
}

function Game({ ctrl, catalog, options, tutorial, onTutorialDone, onReplay, onNew, onQuit }: Props & { ctrl: GameController }) {
  useFrame(ctrl);
  const [prefs, setPrefs] = usePrefs();
  const [tab, setTab] = useState<Tab>('ladder');
  const [showTutorial, setShowTutorial] = useState(tutorial);
  const desktop = useMediaQuery('(min-width: 1024px)');
  const size = prefs.size;
  const goal = catalog.scenarios.find((s) => s.key === options.scenario)?.goal;

  // Hotkeys: B / S market, F flatten, Esc cancel all, 1-9 size.
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.metaKey || e.ctrlKey || e.altKey) return;
      const t = e.target as HTMLElement | null;
      if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.isContentEditable)) return;
      const k = e.key.toLowerCase();
      if (k === 'b') ctrl.buyMarket(size);
      else if (k === 's') ctrl.sellMarket(size);
      else if (k === 'f') ctrl.flatten();
      else if (k === 'escape') ctrl.cancelAll();
      else if (/^[1-9]$/.test(k)) setPrefs({ size: Number(k) });
      else return;
      e.preventDefault();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [ctrl, size, setPrefs]);

  const palette = prefs.colorblind ? 'cb' : 'default';
  const tabs: { key: Tab; label: string }[] = [
    { key: 'ladder', label: 'Book' },
    { key: 'chart', label: 'Chart' },
    { key: 'tape', label: 'Tape' },
    { key: 'news', label: `News${ctrl.news.length ? ` (${ctrl.news.length})` : ''}` },
  ];

  return (
    <div className="flex h-full flex-col">
      <TopBar ctrl={ctrl} prefs={prefs} setPrefs={setPrefs} onQuit={onQuit} goal={goal} />

      {desktop ? (
      /* Desktop: chart | ladder | tape + news, with position and controls below. */
      <div className="grid min-h-0 flex-1 grid-cols-[minmax(0,1fr)_minmax(340px,400px)_300px] grid-rows-[minmax(0,1fr)_auto] gap-2 p-2">
        <div className="min-h-0">
          <PriceChart ctrl={ctrl} palette={palette} />
        </div>
        <div className="row-span-2 min-h-0">
          <Ladder ctrl={ctrl} size={size} />
        </div>
        <div className="grid min-h-0 grid-rows-[minmax(0,1.2fr)_minmax(0,1fr)] gap-2">
          <Tape ctrl={ctrl} />
          <Headlines ctrl={ctrl} />
        </div>
        <div className="min-h-0">
          <PositionPanel ctrl={ctrl} />
        </div>
        <div className="min-h-0">
          <Controls ctrl={ctrl} size={size} onSize={(n) => setPrefs({ size: n })} />
        </div>
      </div>

      ) : (
      /* Mobile and narrow screens: tabs on top, position and controls pinned below. */
      <div className="flex min-h-0 flex-1 flex-col gap-2 p-2">
        <nav className="grid grid-cols-4 gap-1 rounded-md bg-panel p-1" aria-label="Panels">
          {tabs.map((t) => (
            <button
              key={t.key}
              type="button"
              onClick={() => setTab(t.key)}
              aria-pressed={tab === t.key}
              className={`rounded py-1.5 text-sm font-semibold ${tab === t.key ? 'bg-text text-bg' : 'text-muted'}`}
            >
              {t.label}
            </button>
          ))}
        </nav>
        <div className="min-h-[260px] flex-1">
          {tab === 'ladder' && <Ladder ctrl={ctrl} size={size} />}
          {tab === 'chart' && <PriceChart ctrl={ctrl} palette={palette} />}
          {tab === 'tape' && <Tape ctrl={ctrl} rows={30} />}
          {tab === 'news' && <Headlines ctrl={ctrl} />}
        </div>
        <PositionPanel ctrl={ctrl} />
        <Controls ctrl={ctrl} size={size} onSize={(n) => setPrefs({ size: n })} />
      </div>
      )}

      <BreakingNews ctrl={ctrl} />
      <Toasts ctrl={ctrl} />
      <MarginCallOverlay ctrl={ctrl} />
      {prefs.showStats && <StatsPanel ctrl={ctrl} />}
      {showTutorial && !ctrl.summary && (
        <Tutorial
          ctrl={ctrl}
          onDone={() => {
            setShowTutorial(false);
            onTutorialDone();
          }}
        />
      )}
      {ctrl.summary && (
        <Summary ctrl={ctrl} summary={ctrl.summary} goal={goal} onReplay={onReplay} onNew={onNew} onMenu={onQuit} />
      )}
    </div>
  );
}
