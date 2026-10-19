// Newer Node versions ship their own global localStorage, which can shadow
// jsdom's and leave window.localStorage undefined. Give tests a plain
// in-memory Storage when that happens.
if (typeof window !== 'undefined' && !window.localStorage) {
  class MemoryStorage implements Storage {
    private m = new Map<string, string>();
    get length() {
      return this.m.size;
    }
    clear() {
      this.m.clear();
    }
    getItem(k: string) {
      return this.m.has(k) ? this.m.get(k)! : null;
    }
    key(i: number) {
      return [...this.m.keys()][i] ?? null;
    }
    removeItem(k: string) {
      this.m.delete(k);
    }
    setItem(k: string, v: string) {
      this.m.set(k, String(v));
    }
  }
  Object.defineProperty(window, 'localStorage', { value: new MemoryStorage(), configurable: true });
  Object.defineProperty(globalThis, 'Storage', { value: MemoryStorage, configurable: true });
}
