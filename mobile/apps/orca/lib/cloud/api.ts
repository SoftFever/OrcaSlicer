import { getAccessToken, refreshSession } from './auth'
import { CLOUD_API, CLOUD_LIMIT_CODE, CLOUD_MAX_PAYLOAD, CLOUD_PUBLISHABLE_KEY } from './config'

// The preset-sync REST calls, shaped like OrcaCloudServiceAgent's sync_pull /
// sync_push / delete_setting so the two clients stay interchangeable.

export interface CloudUpsert {
  id: string
  name: string
  updated_time: number
  created_time?: number
  content: Record<string, unknown>
}

export interface PullResult {
  upserts: CloudUpsert[]
  deletes: string[]
}

export type PushResult =
  | { kind: 'ok'; updatedTime: number }
  | { kind: 'conflict'; server: { id: string; name: string; updatedTime: number } | null }
  | { kind: 'too_large' }
  | { kind: 'limit' }
  | { kind: 'error'; status: number; message: string }

export interface CloudApi {
  health(): Promise<boolean>
  pull(): Promise<PullResult>
  push(
    id: string,
    name: string,
    content: Record<string, unknown>,
    originalUpdatedTime?: number,
    force?: boolean,
  ): Promise<PushResult>
  remove(id: string): Promise<boolean>
}

export class CloudRequestError extends Error {
  constructor(
    message: string,
    readonly status: number,
  ) {
    super(message)
  }
}

async function request(path: string, init: RequestInit, retry = true): Promise<Response> {
  const token = await getAccessToken()
  if (token === null) throw new CloudRequestError('Not signed in', 401)
  const res = await fetch(`${CLOUD_API}${path}`, {
    ...init,
    headers: {
      apikey: CLOUD_PUBLISHABLE_KEY,
      Authorization: `Bearer ${token}`,
      'Content-Type': 'application/json',
      ...init.headers,
    },
  })
  if (res.status === 401 && retry) {
    const refreshed = await refreshSession()
    if (refreshed === null) throw new CloudRequestError('Signed out', 401)
    return request(path, init, false)
  }
  return res
}

export const cloudApi: CloudApi = {
  async health() {
    try {
      const res = await fetch(`${CLOUD_API}/api/v1/health`, {
        headers: { apikey: CLOUD_PUBLISHABLE_KEY },
      })
      return res.ok
    } catch {
      return false
    }
  },

  // Always a full pull, like the desktop: no cursor, so the result lists every
  // preset the account has and the core can remove local copies of deleted ones.
  async pull() {
    const res = await request('/api/v1/sync/pull', { method: 'GET' })
    if (res.status === 304) return { upserts: [], deletes: [] }
    if (!res.ok) throw new CloudRequestError(`Sync pull failed (${res.status})`, res.status)
    const json = (await res.json()) as { upserts?: CloudUpsert[]; deletes?: string[] }
    return { upserts: json.upserts ?? [], deletes: json.deletes ?? [] }
  },

  async push(id, name, content, originalUpdatedTime, force = false) {
    const body: Record<string, unknown> = { id, name, content }
    // The desktop sends the version check as a string; the server reads either.
    if (originalUpdatedTime !== undefined && originalUpdatedTime > 0)
      body.original_updated_time = String(originalUpdatedTime)
    const text = JSON.stringify(body)
    if (text.length > CLOUD_MAX_PAYLOAD) return { kind: 'too_large' }
    const res = await request(force ? '/api/v1/sync/force-push' : '/api/v1/sync/push', {
      method: 'POST',
      body: text,
    })
    const raw = await res.text()
    let json: Record<string, unknown> | null = null
    try {
      json = JSON.parse(raw) as Record<string, unknown> | null
    } catch {
      /* not JSON */
    }
    if (res.status === 200) {
      const updatedTime = Number(json?.updated_time ?? 0)
      return updatedTime > 0
        ? { kind: 'ok', updatedTime }
        : { kind: 'error', status: 200, message: 'No updated_time in the response' }
    }
    if (res.status === 409) {
      const sp = (json?.server_profile ?? null) as {
        id?: string
        name?: string
        updated_time?: number
      } | null
      return {
        kind: 'conflict',
        server:
          sp === null
            ? null
            : { id: sp.id ?? id, name: sp.name ?? name, updatedTime: Number(sp.updated_time ?? 0) },
      }
    }
    if (res.status === 413) return { kind: 'too_large' }
    if (String(json?.code ?? '') === CLOUD_LIMIT_CODE) return { kind: 'limit' }
    return {
      kind: 'error',
      status: res.status,
      message: (json?.message as string | undefined) ?? raw.slice(0, 200),
    }
  },

  async remove(id) {
    const res = await request(
      `/api/v1/sync/delete?resource=profiles&id=${encodeURIComponent(id)}`,
      { method: 'DELETE' },
    )
    // 404 means it is already gone; the local sidecar can go too.
    return res.ok || res.status === 204 || res.status === 404
  },
}
