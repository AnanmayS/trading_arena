import { useEffect, useState } from 'react';
import { BtbEngine, loadModule, type Frame } from './engine/engine';
import { ticksToPoints } from './engine/format';

// Milestone 4: minimal page proving the WASM engine runs in the browser.
export function App() {
  const [frame, setFrame] = useState<Frame | null>(null);
  const [trades, setTrades] = useState(0);

  useEffect(() => {
    let raf = 0;
    let engine: BtbEngine | null = null;
    let last = performance.now();
    let cancelled = false;
    loadModule().then((mod) => {
      if (cancelled) return;
      engine = new BtbEngine(mod, { seed: 42, difficulty: 'normal' });
      const loop = (now: number) => {
        engine!.step(now - last);
        last = now;
        const { frame: f, events } = engine!.refresh();
        setFrame(f);
        setTrades((t) => t + events.filter((e) => e.type === 5).length);
        raf = requestAnimationFrame(loop);
      };
      raf = requestAnimationFrame(loop);
    });
    return () => {
      cancelled = true;
      cancelAnimationFrame(raf);
      engine?.dispose();
    };
  }, []);

  if (!frame) return <p className="p-4 font-mono">loading engine…</p>;
  return (
    <div className="p-4 font-mono text-sm">
      <p>t={(frame.timeUs / 1e6).toFixed(1)}s mark {ticksToPoints(frame.mark)} trades {trades}</p>
      <table>
        <tbody>
          {[...frame.asks].reverse().map((l) => (
            <tr key={`a${l.price}`} className="text-red-600">
              <td className="pr-4">{ticksToPoints(l.price)}</td>
              <td>{l.qty}</td>
            </tr>
          ))}
          {frame.bids.map((l) => (
            <tr key={`b${l.price}`} className="text-green-600">
              <td className="pr-4">{ticksToPoints(l.price)}</td>
              <td>{l.qty}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
