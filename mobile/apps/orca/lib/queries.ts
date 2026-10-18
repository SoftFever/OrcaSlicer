import { QueryClient } from '@tanstack/react-query'

// Shared query client. Kept minimal on purpose: the mobile app talks to a
// handful of long-lived resources (the GitHub profiles source, Moonraker /
// OctoPrint hosts) and its cache does not need to grow beyond a session.
export const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      // GitHub throttles anonymous requests; refetching on every focus would
      // burn the budget within a few minutes on the vendors screen. The
      // per-hook staleTime overrides this when needed (e.g. printer status).
      staleTime: 5 * 60 * 1000,
      gcTime: 30 * 60 * 1000,
      retry: 2,
      refetchOnWindowFocus: false,
    },
  },
})

// Query keys are strings up front so they are easy to invalidate from
// anywhere (e.g. after installing a vendor).
export const queryKeys = {
  availableVendors: ['profiles', 'available'] as const,
  availablePrinters: ['profiles', 'printers'] as const,
  installedVendors: ['profiles', 'installed'] as const,
  printerStatus: (id: string) => ['printers', id, 'status'] as const,
}
