import { create } from 'zustand'

// Tiny toast bus. Screens call `showToast('Saved plate.3mf')`; a single overlay
// in `_layout.tsx` renders whatever the store holds. We keep the API stringly
// so screens can throw them in from anywhere without pulling a component down.

export type ToastKind = 'info' | 'success' | 'error'

export interface Toast {
  id: number
  message: string
  kind: ToastKind
}

interface ToastState {
  toasts: Toast[]
  show: (message: string, kind?: ToastKind) => void
  dismiss: (id: number) => void
}

let nextId = 1

export const useToastStore = create<ToastState>((set, get) => ({
  toasts: [],
  show: (message, kind = 'info') => {
    const id = nextId++
    set((s) => ({ toasts: [...s.toasts, { id, message, kind }] }))
    // Auto-dismiss after 3.2s. Error toasts hold for 5s so the user has time
    // to read them; explicit dismiss via the caller stays instant.
    const ttl = kind === 'error' ? 5000 : 3200
    setTimeout(() => get().dismiss(id), ttl)
  },
  dismiss: (id) => set((s) => ({ toasts: s.toasts.filter((t) => t.id !== id) })),
}))

/** Fire-and-forget from anywhere; no React context needed. */
export function showToast(message: string, kind: ToastKind = 'info'): void {
  useToastStore.getState().show(message, kind)
}
