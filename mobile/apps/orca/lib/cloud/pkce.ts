import * as Crypto from 'expo-crypto'

// PKCE (RFC 7636) values for the Orca Cloud sign-in page, S256 like the desktop.

export interface Pkce {
  codeVerifier: string
  codeChallenge: string
  state: string
}

function base64url(bytes: Uint8Array): string {
  let bin = ''
  for (const b of bytes) bin += String.fromCharCode(b)
  return btoa(bin).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '')
}

export async function createPkce(): Promise<Pkce> {
  const verifierBytes = Crypto.getRandomBytes(32)
  const codeVerifier = base64url(verifierBytes) // 43 chars, within the 43–128 the RFC asks for
  const digest = await Crypto.digest(Crypto.CryptoDigestAlgorithm.SHA256, new TextEncoder().encode(codeVerifier))
  const codeChallenge = base64url(new Uint8Array(digest))
  const state = Array.from(Crypto.getRandomBytes(16), (b) => b.toString(16).padStart(2, '0')).join('')
  return { codeVerifier, codeChallenge, state }
}
