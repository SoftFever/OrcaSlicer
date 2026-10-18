import { Stack, useRouter } from 'expo-router'
import React, { useCallback, useEffect, useRef, useState } from 'react'
import { ActivityIndicator, Linking, Pressable, Text, View } from 'react-native'
import { WebView, type WebViewMessageEvent, type WebViewNavigation } from 'react-native-webview'

import { adoptSession, exchangeCode } from '@/lib/cloud/auth'
import { CLOUD_AUTH, CLOUD_LOOPBACK_PORT, CLOUD_PUBLISHABLE_KEY, CLOUD_REDIRECT, CLOUD_WEB, cloudUserAgent } from '@/lib/cloud/config'
import { createPkce, type Pkce } from '@/lib/cloud/pkce'
import { useCore } from '@/lib/core'
import { currentLanguage } from '@/lib/i18n'

// The desktop's embedded sign-in page, driven the way WebUserLoginDialog drives
// it. The page asks the host for its login config through window.wx.postMessage
// ({command: 'get_login_cmd'}); the host answers with a window.postMessage that
// carries the PKCE challenge and the redirect. The page then either hands back
// an auth code ('user_login' with data.code) or jumps to a third-party provider
// ('thirdparty_login' with data.url), whose flow ends on the loopback redirect.
// There is no loopback server on iOS, so the navigation to it is intercepted
// here and the code exchanged directly.

const BRIDGE = `
(function () {
  window.wx = { postMessage: function (m) { window.ReactNativeWebView.postMessage(typeof m === 'string' ? m : JSON.stringify(m)) } }
  window.__orcaPost = function (obj) { window.postMessage(obj, '*') }
})(); true;`

export default function CloudLoginScreen(): React.JSX.Element {
  const router = useRouter()
  const { version, setCloudUser } = useCore()
  const web = useRef<React.ComponentRef<typeof WebView>>(null)
  const [pkce, setPkce] = useState<Pkce | null>(null)
  const [finishing, setFinishing] = useState(false)
  const done = useRef(false)

  useEffect(() => {
    void createPkce().then(setPkce)
  }, [])

  const finish = useCallback(
    async (work: () => Promise<{ user: { id: string; email: string; displayName: string; avatarUrl: string } }>) => {
      if (done.current) return
      done.current = true
      setFinishing(true)
      try {
        const session = await work()
        await setCloudUser(session.user)
        router.back()
      } catch (e) {
        done.current = false
        setFinishing(false)
        web.current?.injectJavaScript(`alert(${JSON.stringify(`Sign-in failed: ${String(e)}`)}); true;`)
      }
    },
    [router, setCloudUser],
  )

  const reply = useCallback((obj: unknown) => {
    web.current?.injectJavaScript(`window.__orcaPost(${JSON.stringify(obj)}); true;`)
  }, [])

  const onMessage = useCallback(
    (ev: WebViewMessageEvent) => {
      if (pkce === null) return
      let msg: Record<string, unknown>
      try {
        msg = JSON.parse(ev.nativeEvent.data) as Record<string, unknown>
      } catch {
        return
      }
      const command = msg.command as string | undefined
      const data = (msg.data ?? {}) as Record<string, unknown>
      switch (command) {
        case 'get_login_cmd':
          reply({
            action: 'login_config',
            backend_url: CLOUD_AUTH,
            apikey: CLOUD_PUBLISHABLE_KEY,
            pkce: {
              code_challenge: pkce.codeChallenge,
              code_challenge_method: 'S256',
              state: pkce.state,
              redirect_uri: CLOUD_REDIRECT,
              loopback_port: CLOUD_LOOPBACK_PORT,
            },
          })
          break
        case 'get_localhost_url':
          reply({ command: 'get_localhost_url', sequence_id: msg.sequence_id, response: { base_url: `http://localhost:${CLOUD_LOOPBACK_PORT}`, result: 'success' } })
          break
        case 'user_login': {
          const code = data.code as string | undefined
          if (typeof code === 'string' && code.length > 0) {
            void finish(() => exchangeCode(code, pkce.codeVerifier))
          } else if (typeof data.access_token === 'string' && typeof data.refresh_token === 'string') {
            void finish(() => adoptSession(data as never))
          } else if (typeof (data.session as Record<string, unknown> | undefined)?.access_token === 'string') {
            void finish(() => adoptSession(data.session as never))
          }
          break
        }
        case 'thirdparty_login': {
          // The desktop opens the system browser here and catches the loopback
          // redirect with a local server. Stay in this WebView instead; the
          // redirect is intercepted below.
          const url = data.url as string | undefined
          if (typeof url === 'string') web.current?.injectJavaScript(`window.location.href = ${JSON.stringify(url)}; true;`)
          break
        }
        case 'new_webpage': {
          const url = data.url as string | undefined
          if (typeof url === 'string') void Linking.openURL(url)
          break
        }
        default:
          break
      }
    },
    [pkce, reply, finish],
  )

  const onShouldStartLoad = useCallback(
    (nav: WebViewNavigation): boolean => {
      const m = /^https?:\/\/(?:localhost|127\.0\.0\.1):4117[2-4]\/callback(?:\?(.*))?$/.exec(nav.url)
      if (m === null) return true
      if (pkce === null) return false
      const params = new URLSearchParams(m[1] ?? '')
      const code = params.get('code')
      const state = params.get('orca_state') ?? params.get('state')
      if (code !== null && (state === null || state === pkce.state)) {
        void finish(() => exchangeCode(code, pkce.codeVerifier))
      }
      return false
    },
    [pkce, finish],
  )

  return (
    <View className="flex-1 bg-white dark:bg-neutral-950">
      <Stack.Screen
        options={{
          title: 'Sign in to Orca Cloud',
          headerRight: () => (
            <Pressable onPress={() => router.back()} hitSlop={8}>
              <Text className="text-subhead font-semibold text-blue-500">Cancel</Text>
            </Pressable>
          ),
        }}
      />
      {pkce === null || finishing ? (
        <View className="flex-1 items-center justify-center">
          <ActivityIndicator />
        </View>
      ) : (
        <WebView
          ref={web}
          source={{ uri: `${CLOUD_WEB}/orcaslicer-login?lang=${encodeURIComponent(currentLanguage())}` }}
          style={{ flex: 1 }}
          userAgent={cloudUserAgent(version)}
          injectedJavaScriptBeforeContentLoaded={BRIDGE}
          onMessage={onMessage}
          onShouldStartLoadWithRequest={onShouldStartLoad}
          sharedCookiesEnabled
          javaScriptEnabled
        />
      )}
    </View>
  )
}
