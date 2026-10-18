import * as Notifications from 'expo-notifications'

let requested = false

// Fire-and-forget helper. iOS gates local notifications behind a permission
// prompt; the first fire triggers the request. Foreground presentation is
// always on so the user sees a banner even without leaving the app.
Notifications.setNotificationHandler({
  handleNotification: async () => ({
    shouldShowBanner: true,
    shouldShowList: true,
    shouldPlaySound: false,
    shouldSetBadge: false,
  }),
})

export async function notify(title: string, body?: string): Promise<void> {
  if (!requested) {
    requested = true
    const settings = await Notifications.getPermissionsAsync()
    if (settings.status !== 'granted') {
      const req = await Notifications.requestPermissionsAsync()
      if (req.status !== 'granted') return
    }
  }
  await Notifications.scheduleNotificationAsync({
    content: { title, body },
    trigger: null,
  })
}
