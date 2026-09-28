/** Sliding-window failure limiter for LAN-facing auth endpoints.
 *
 * Keys are client IPs. A key is blocked once it recorded `max` failures inside
 * `windowMs`; success paths call reset(). Entries are pruned lazily on access,
 * so abandoned keys linger until their window is queried again — fine at LAN
 * scale, and the map cannot grow beyond distinct recent IPs.
 */
export class FailureLimiter {
  private readonly fails = new Map<string, number[]>();

  constructor(
    private readonly max: number,
    private readonly windowMs: number,
  ) {}

  isBlocked(key: string, now: Date): boolean {
    return this.prune(key, now.getTime()).length >= this.max;
  }

  recordFailure(key: string, now: Date): void {
    const list = this.prune(key, now.getTime());
    list.push(now.getTime());
    this.fails.set(key, list);
  }

  reset(key: string): void {
    this.fails.delete(key);
  }

  private prune(key: string, nowMs: number): number[] {
    const list = (this.fails.get(key) ?? []).filter((t) => nowMs - t < this.windowMs);
    this.fails.set(key, list);
    return list;
  }
}
