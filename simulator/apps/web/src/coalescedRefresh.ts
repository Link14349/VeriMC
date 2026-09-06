// Keep one read in flight and one remembered invalidation. An update arriving
// during a read must cause another read, even when no later update follows it.
export function createCoalescedRefresh<T>(read: () => Promise<T>, apply: (value: T) => void, reportError: (error: unknown) => void) {
  let active = true, running = false, dirty = false;
  const drain = async () => {
    running = true;
    try {
      while (active && dirty) {
        dirty = false;
        try {
          const value = await read();
          if (active) apply(value);
        } catch (error) {
          if (active) reportError(error);
        }
      }
    } finally { running = false; }
  };
  return {
    request() { if (active) { dirty = true; if (!running) void drain(); } },
    dispose() { active = false; dirty = false; }
  };
}
