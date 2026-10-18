import * as SecureStore from 'expo-secure-store'

import { CLOUD_AUTH, CLOUD_PUBLISHABLE_KEY } from './config'
import { storage } from '../storage'

// Orca Cloud sign-in state. Tokens live in the Keychain (expo-secure-store);
// the profile the UI shows, and the user id the core needs before it loads
// presets, live in MMKV so they can be read synchronously at boot.

export interface CloudUser {
  id: string
  email: string
  displayName: string
  avatarUrl: string
}

export interface CloudSession {
  accessToken: string
  refreshToken: string
  /** Unix seconds. */
  expiresAt: number
  user: CloudUser
}

const SECURE_KEY = 'orca.cloud.session'
const USER_KEY = 'cloud:v1:user'

let cached: CloudSession | null | undefined
let refreshing: Promise<CloudSession | null> | null = null

/** The signed-in user, or null. Synchronous: read from MMKV. */
export function storedCloudUser(): CloudUser | null {
  const raw = storage.getString(USER_KEY)
  if (raw === undefined) return null
  try {
    return JSON.parse(raw) as CloudUser
  } catch {
    return null
  }
}

/** The user id the core should load presets for: '' when signed out. */
export function storedCloudUserId(): string {
  return storedCloudUser()?.id ?? ''
}

async function readSession(): Promise<CloudSession | null> {
  if (cached !== undefined) return cached
  try {
    const raw = await SecureStore.getItemAsync(SECURE_KEY)
    cached = raw === null ? null : (JSON.parse(raw) as CloudSession)
  } catch {
    cached = null
  }
  return cached
}

async function writeSession(session: CloudSession | null): Promise<void> {
  cached = session
  if (session === null) {
    await SecureStore.deleteItemAsync(SECURE_KEY).catch(() => undefined)
    storage.remove(USER_KEY)
  } else {
    await SecureStore.setItemAsync(SECURE_KEY, JSON.stringify(session))
    storage.set(USER_KEY, JSON.stringify(session.user))
  }
}

interface TokenResponse {
  access_token: string
  refresh_token: string
  expires_in?: number
  expires_at?: number
  user?: {
    id: string
    email?: string
    user_metadata?: { username?: string; display_name?: string; full_name?: string; name?: string; avatar_url?: string }
  }
}

function sessionFrom(body: TokenResponse, previous?: CloudUser): CloudSession {
  const meta = body.user?.user_metadata ?? {}
  const user: CloudUser = body.user !== undefined
    ? {
        id: body.user.id,
        email: body.user.email ?? previous?.email ?? '',
        displayName: meta.display_name ?? meta.full_name ?? meta.name ?? meta.username ?? body.user.email ?? previous?.displayName ?? '',
        avatarUrl: meta.avatar_url ?? previous?.avatarUrl ?? '',
      }
    : (previous ?? { id: '', email: '', displayName: '', avatarUrl: '' })
  const expiresAt = body.expires_at ?? Math.floor(Date.now() / 1000) + (body.expires_in ?? 3600)
  return { accessToken: body.access_token, refreshToken: body.refresh_token, expiresAt, user }
}

export class CloudAuthError extends Error {
  constructor(message: string, readonly status: number) {
    super(message)
  }
}

async function tokenRequest(grant: string, body: Record<string, string>, previous?: CloudUser): Promise<CloudSession> {
  const res = await fetch(`${CLOUD_AUTH}/auth/v1/token?grant_type=${grant}`, {
    method: 'POST',
    headers: { apikey: CLOUD_PUBLISHABLE_KEY, 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  })
  const text = await res.text()
  let json: Record<string, unknown> = {}
  try { json = JSON.parse(text) as Record<string, unknown> } catch { /* not JSON */ }
  if (!res.ok) {
    const message = (json.error_description ?? json.msg ?? json.error ?? json.message ?? text) as string
    throw new CloudAuthError(typeof message === 'string' && message.length > 0 ? message : `Sign-in failed (${res.status})`, res.status)
  }
  return sessionFrom(json as unknown as TokenResponse, previous)
}

/** Email + password sign-in (the GoTrue password grant). */
export async function signInWithPassword(email: string, password: string): Promise<CloudSession> {
  const session = await tokenRequest('password', { email: email.trim(), password })
  await writeSession(session)
  return session
}

/** Exchanges the code the sign-in page handed back, with the PKCE verifier it was issued for. */
export async function exchangeCode(code: string, codeVerifier: string): Promise<CloudSession> {
  const session = await tokenRequest('pkce', { auth_code: code, code_verifier: codeVerifier })
  await writeSession(session)
  return session
}

/** The sign-in page can also hand over a finished session (tokens) instead of a code. */
export async function adoptSession(body: TokenResponse): Promise<CloudSession> {
  const session = sessionFrom(body)
  await writeSession(session)
  return session
}

/**
 * Refreshes the access token. Refresh tokens rotate on every use, so the
 * result is written before anything else runs, and concurrent callers share one
 * request. A rejected refresh (revoked, reused) signs the user out.
 */
export async function refreshSession(): Promise<CloudSession | null> {
  if (refreshing !== null) return refreshing
  refreshing = (async () => {
    const current = await readSession()
    if (current === null) return null
    try {
      const next = await tokenRequest('refresh_token', { refresh_token: current.refreshToken }, current.user)
      await writeSession(next)
      return next
    } catch (e) {
      if (e instanceof CloudAuthError && e.status >= 400 && e.status < 500) {
        await writeSession(null)
        return null
      }
      throw e
    } finally {
      refreshing = null
    }
  })()
  return refreshing
}

/** A valid access token, refreshing when it is within 15 minutes of expiry. Null when signed out. */
export async function getAccessToken(): Promise<string | null> {
  const session = await readSession()
  if (session === null) return null
  if (session.expiresAt - Date.now() / 1000 < 15 * 60) {
    const next = await refreshSession()
    return next?.accessToken ?? null
  }
  return session.accessToken
}

export async function isSignedIn(): Promise<boolean> {
  return (await readSession()) !== null
}

/** Tells the server, then forgets the session whatever the server said. */
export async function signOut(): Promise<void> {
  const session = await readSession()
  if (session !== null) {
    await fetch(`${CLOUD_AUTH}/auth/v1/logout`, {
      method: 'POST',
      headers: { apikey: CLOUD_PUBLISHABLE_KEY, Authorization: `Bearer ${session.accessToken}` },
    }).catch(() => undefined)
  }
  await writeSession(null)
}
