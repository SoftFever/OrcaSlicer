// Orca Cloud endpoints, as the desktop uses them (src/slic3r/Utils/OrcaCloudServiceAgent.cpp).
// The API is unofficial: everything that could change lives here.

/** REST API: preset sync lives under /api/v1/sync. */
export const CLOUD_API = 'https://api.orcaslicer.com'
/** Supabase GoTrue auth server. */
export const CLOUD_AUTH = 'https://auth.orcaslicer.com'
/** The web app; hosts the sign-in page the desktop embeds. */
export const CLOUD_WEB = 'https://cloud.orcaslicer.com'
/** Supabase publishable key; sent as `apikey` on every request. Public, not a secret. */
export const CLOUD_PUBLISHABLE_KEY = 'sb_publishable_lvVe_whOi80SU9BPSxM1kA_tbt9AbR_'
/** The only OAuth callback the backend knows: the desktop's loopback listener. The WebView intercepts it instead. */
export const CLOUD_REDIRECT = 'http://localhost:41172/callback'
export const CLOUD_LOOPBACK_PORT = 41172
/** The server rejects push bodies above this; the desktop checks before sending. */
export const CLOUD_MAX_PAYLOAD = 1024 * 1024
/** Error `code` in a push response that means the account's preset quota is full. */
export const CLOUD_LIMIT_CODE = '14'

/** The desktop identifies as Safari plus a slicer tag; the login page keys off it. */
export function cloudUserAgent(coreVersion: string): string {
  return `Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15 BBL-Slicer/v${coreVersion} (dark) BBL-Language/en`
}
