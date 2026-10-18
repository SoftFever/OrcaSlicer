import * as BackgroundTask from 'expo-background-task'
import * as TaskManager from 'expo-task-manager'

import { useDownloadsStore, type Download } from './downloads'

// The task the OS wakes us for. Its job is minimal: if any downloads were in
// flight when we were suspended, wait a short bounded time for them to
// finish, then report BackgroundTask success. iOS gives a task ~30 seconds
// of wall time; we cap the wait accordingly.
const TASK_NAME = 'orca-downloads-continue'

TaskManager.defineTask(TASK_NAME, async () => {
  const active = Object.values(useDownloadsStore.getState().byId).filter(
    (d: Download) => d.state === 'downloading' || d.state === 'installing',
  )
  if (active.length === 0) return BackgroundTask.BackgroundTaskResult.Success
  await Promise.race([
    Promise.all(active.map((d) => d.finished)),
    new Promise((r) => setTimeout(r, 25_000)),
  ])
  return BackgroundTask.BackgroundTaskResult.Success
})

let registered = false

/** Ask the OS to wake us periodically so pending downloads can resume. Idempotent. */
export async function ensureBackgroundTaskRegistered(): Promise<void> {
  if (registered) return
  registered = true
  try {
    const status = await BackgroundTask.getStatusAsync()
    if (status === BackgroundTask.BackgroundTaskStatus.Restricted) return
    const already = await TaskManager.isTaskRegisteredAsync(TASK_NAME)
    if (!already) {
      await BackgroundTask.registerTaskAsync(TASK_NAME, {
        // 15 minutes is the effective iOS floor; the OS coalesces anyway.
        minimumInterval: 15,
      })
    }
  } catch {
    // Background task is a nice-to-have; a foreground-only install still
    // completes when the app is in focus. Never let this block startup.
  }
}
