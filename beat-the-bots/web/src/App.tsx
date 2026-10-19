import { useEffect, useMemo, useState } from 'react';
import type { BtbModule } from './engine/wasm/btb.js';
import { BtbEngine, loadModule, type Catalog, type GameOptions } from './engine/engine';
import { GameScreen } from './components/GameScreen';
import { Landing } from './components/Landing';
import { Keys, readJson, writeJson } from './game/storage';
import { parseLaunch, randomSeed, shareUrl } from './game/url';

type Screen = { kind: 'landing' } | { kind: 'game'; options: GameOptions; tutorial: boolean; key: number };

export function App() {
  const launch = useMemo(() => parseLaunch(window.location.search), []);
  const [mod, setMod] = useState<BtbModule | null>(null);
  const [catalog, setCatalog] = useState<Catalog | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [screen, setScreen] = useState<Screen>({ kind: 'landing' });
  const [tutorialDone, setTutorialDone] = useState(() => readJson<boolean>(Keys.tutorialDone, false));
  // A shared link's seed is offered once; after that PLAY rolls new markets.
  const [challenge, setChallenge] = useState(() =>
    launch.seed ? { seed: launch.seed, difficulty: launch.difficulty ?? 'normal', scenario: launch.scenario } : null,
  );

  // Start fetching and compiling the engine immediately so PLAY is instant.
  useEffect(() => {
    loadModule()
      .then((m) => {
        setCatalog(BtbEngine.catalog(m));
        setMod(m);
      })
      .catch((e: unknown) => setError(e instanceof Error ? e.message : String(e)));
  }, []);

  const start = (options: GameOptions, tutorial: boolean) => {
    // Put the market in the address bar so a refresh or copied URL replays it.
    window.history.replaceState(null, '', shareUrl(window.location.href, options.seed, options.difficulty, options.scenario));
    setScreen({ kind: 'game', options, tutorial, key: Date.now() });
    setChallenge(null);
  };

  if (error) {
    return (
      <main className="p-8">
        <h1 className="text-xl font-bold">The engine failed to load</h1>
        <p className="mt-2 text-muted">{error}</p>
        <p className="mt-2 text-sm text-muted">This game needs a browser with WebAssembly support.</p>
      </main>
    );
  }

  if (screen.kind === 'game' && mod && catalog) {
    const { options } = screen;
    return (
      <GameScreen
        key={screen.key}
        mod={mod}
        catalog={catalog}
        options={options}
        tutorial={screen.tutorial}
        onTutorialDone={() => {
          setTutorialDone(true);
          writeJson(Keys.tutorialDone, true);
        }}
        onReplay={() => start({ ...options }, false)}
        onNew={() => start({ ...options, seed: randomSeed(), scenario: undefined }, false)}
        onQuit={() => setScreen({ kind: 'landing' })}
      />
    );
  }

  return (
    <Landing
      ready={!!mod}
      catalog={catalog}
      challenge={challenge}
      roundSeconds={launch.roundSeconds}
      tutorialDone={tutorialDone}
      onPlay={start}
    />
  );
}
