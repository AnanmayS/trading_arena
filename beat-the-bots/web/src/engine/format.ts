// Display-only conversions. The engine works in integer ticks and cents; this
// is the one place they become points and dollars.

export function ticksToPoints(ticks: number, ticksPerPoint = 4): string {
  return (ticks / ticksPerPoint).toFixed(2);
}

const usd = new Intl.NumberFormat('en-US', { style: 'currency', currency: 'USD', minimumFractionDigits: 2 });
const usd0 = new Intl.NumberFormat('en-US', { style: 'currency', currency: 'USD', maximumFractionDigits: 0 });

export function centsToDollars(cents: number, whole = false): string {
  return (whole ? usd0 : usd).format(cents / 100);
}

export function signedDollars(cents: number, whole = false): string {
  const s = centsToDollars(Math.abs(cents), whole);
  if (cents > 0) return `+${s}`;
  if (cents < 0) return `-${s}`;
  return s;
}

export function formatClock(us: number): string {
  const total = Math.max(0, Math.ceil(us / 1_000_000));
  const m = Math.floor(total / 60);
  const s = total % 60;
  return `${m}:${s.toString().padStart(2, '0')}`;
}

export function pct(bp: number): string {
  const v = bp / 100;
  return `${v > 0 ? '+' : ''}${v.toFixed(2)}%`;
}
