// Relative time formatting for Recent lists. Falls back to a short absolute
// date when the delta is more than a week — beyond that "3 weeks ago" reads
// as noise and users want to see the actual date.

const MINUTE = 60_000
const HOUR   = 60 * MINUTE
const DAY    = 24 * HOUR
const WEEK   = 7 * DAY

export function relativeTime(ts: number, now: number = Date.now()): string {
  const dt = Math.max(0, now - ts)
  if (dt < MINUTE) return 'just now'
  if (dt < HOUR)   { const m = Math.floor(dt / MINUTE); return `${m} min${m === 1 ? '' : 's'} ago` }
  if (dt < DAY)    { const h = Math.floor(dt / HOUR);   return `${h} hour${h === 1 ? '' : 's'} ago` }
  if (dt < WEEK)   { const d = Math.floor(dt / DAY);    return `${d} day${d === 1 ? '' : 's'} ago` }
  const date = new Date(ts)
  return date.toLocaleDateString(undefined, { month: 'short', day: 'numeric' })
}
