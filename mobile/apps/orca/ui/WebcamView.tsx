import React from 'react'
import { View } from 'react-native'
import { WebView } from 'react-native-webview'

// MJPEG streams (`multipart/x-mixed-replace`) do not decode through iOS
// UIImage, so we wrap the stream URL in a tiny HTML shim that binds it to
// an <img> — the WebView's WKWebView keeps feeding frames as they arrive.
// Falls back to a neutral placeholder background when a load fails so the
// card never renders as a blank rectangle.

type Props = {
  streamUrl: string
  height?: number
}

function html(streamUrl: string): string {
  return `<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'/><style>
    html,body{margin:0;padding:0;background:#111;height:100%;width:100%;overflow:hidden}
    img{width:100%;height:100%;object-fit:contain;display:block}
  </style></head><body><img src="${streamUrl}"/></body></html>`
}

export function WebcamView({ streamUrl, height = 220 }: Props): React.JSX.Element {
  return (
    <View style={{ height, backgroundColor: '#111', borderRadius: 20, overflow: 'hidden' }}>
      <WebView
        originWhitelist={['*']}
        source={{ html: html(streamUrl) }}
        allowsInlineMediaPlayback
        mediaPlaybackRequiresUserAction={false}
        scrollEnabled={false}
        bounces={false}
        style={{ backgroundColor: '#111' }}
      />
    </View>
  )
}
