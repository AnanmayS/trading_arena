import { describe, expect, it } from 'vitest';
import { parseLaunch, shareUrl } from '../src/game/url';

describe('share links', () => {
  it('round-trips seed, difficulty and scenario', () => {
    const url = shareUrl('https://example.com/play?x=1#h', 1234, 'hard', 'flash_crash');
    expect(url).toBe('https://example.com/play?seed=1234&diff=hard&scenario=flash_crash');
    expect(parseLaunch(new URL(url).search)).toEqual({ seed: 1234, difficulty: 'hard', scenario: 'flash_crash', roundSeconds: null });
  });

  it('rejects junk parameters', () => {
    expect(parseLaunch('?seed=-4&diff=insane&scenario=<script>&t=2')).toEqual({
      seed: null,
      difficulty: null,
      scenario: null,
      roundSeconds: null,
    });
    expect(parseLaunch('?seed=12.5').seed).toBeNull();
    expect(parseLaunch('?t=20').roundSeconds).toBe(20);
  });

  it('omits the scenario for regular rounds', () => {
    expect(shareUrl('https://x.dev/', 7, 'easy', 'none')).toBe('https://x.dev/?seed=7&diff=easy');
  });
});
