// Prints determinism hashes from the WebAssembly build for the same matrix as
// `btb_cli --selftest`. CI diffs the two outputs: identical lines prove the
// native and WASM engines produce byte-identical event streams.
import createBtbModule from '../src/engine/wasm/btb.js';

const configs = [
  '{"difficulty":"normal","round_seconds":60}',
  '{"difficulty":"hard","round_seconds":60}',
  '{"difficulty":"easy","scenario":"flash_crash","round_seconds":90}',
  '{"difficulty":"normal","scenario":"news_day","round_seconds":60}',
];

const mod = await createBtbModule();
for (const seed of [1, 42, 1987]) {
  for (const c of configs) {
    console.log(`${seed} ${c} ${mod.Engine.selfTest(seed, c)}`);
  }
}
