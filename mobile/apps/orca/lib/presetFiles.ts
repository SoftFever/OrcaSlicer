import * as DocumentPicker from 'expo-document-picker'
import { Directory, File, Paths } from 'expo-file-system'
import * as Sharing from 'expo-sharing'
import { Alert } from 'react-native'
import type {
  ImportPresetsResult,
  OrcaSession,
  PresetKind,
  PresetRef,
  SkippedPreset,
} from 'react-native-orca-core'

import { useDownloadsStore } from './downloads'
import {
  availablePrinters,
  installedVendors,
  installVendor,
  type AvailablePrinter,
} from './profiles'
import { queryClient, queryKeys } from './queries'
import { writeRememberedPresets } from './prefs'
import { showToast } from './toast'

// Desktop preset files in and out of the app. The core does the parsing, parent
// resolution and saving (Session::import_presets / export_presets); this module
// owns the conversation with the user: overwrite prompts, the "install the vendor
// first" detour, and the summary toast.

/** File extensions the core's importer accepts. */
export const PRESET_FILE_PATTERN = /\.(orca_printer|orca_filament|orca_bundle|zip|json)$/i

export const PRESET_UTIS = {
  printer: 'io.github.angelohub.orcaslicer.orca-printer',
  filament: 'io.github.angelohub.orcaslicer.orca-filament',
  bundle: 'io.github.angelohub.orcaslicer.orca-bundle',
} as const

export interface ImportContext {
  session: OrcaSession
  reloadPresets: () => Promise<void>
  /** Re-reads the session's selections into React state; the home screen's refresh(). */
  onChanged?: () => void
}

const KIND_LABEL: Record<PresetKind, string> = {
  printer: 'printer',
  filament: 'filament',
  process: 'process',
}

/** One line for the toast: "Imported 1 printer, 3 filaments and 2 processes". */
export function describeImported(imported: PresetRef[]): string {
  const counts: Record<PresetKind, number> = { printer: 0, filament: 0, process: 0 }
  for (const p of imported) counts[p.kind] += 1
  const parts = (['printer', 'filament', 'process'] as const)
    .filter((k) => counts[k] > 0)
    .map(
      (k) => `${counts[k]} ${KIND_LABEL[k]}${counts[k] === 1 ? '' : k === 'process' ? 'es' : 's'}`,
    )
  if (parts.length === 0) return 'Nothing imported'
  if (parts.length === 1) return `Imported ${parts[0]}`
  return `Imported ${parts.slice(0, -1).join(', ')} and ${parts[parts.length - 1]}`
}

/** The vendor a result says is missing, when every missing parent points at one vendor. */
export function missingVendorIn(result: Pick<ImportPresetsResult, 'skipped'>): string | null {
  const hints = new Set(
    result.skipped
      .filter((s) => s.reason === 'missing_parent' && s.vendorHint !== '')
      .map((s) => s.vendorHint),
  )
  return hints.size === 1 ? [...hints][0]! : null
}

/** Human text for the presets the core refused, grouped by reason. */
export function describeSkipped(skipped: SkippedPreset[]): string[] {
  const lines: string[] = []
  const byReason = (reason: SkippedPreset['reason']) => skipped.filter((s) => s.reason === reason)
  const system = byReason('system_name')
  if (system.length > 0)
    lines.push(`Built-in presets were left alone: ${system.map((s) => s.name).join(', ')}`)
  const orphans = byReason('missing_parent')
  if (orphans.length > 0)
    lines.push(
      `Parent preset not installed: ${orphans.map((s) => `${s.name} (needs ${s.inherits})`).join('; ')}`,
    )
  const invalid = byReason('invalid')
  if (invalid.length > 0) lines.push(`Not a preset: ${invalid.map((s) => s.name).join(', ')}`)
  return lines
}

/**
 * Guesses which vendor ships the parent preset named in `inherits`, from the
 * catalogue of printers the vendors screen already fetches. The desktop's
 * vendor lookup only sees vendors that are on disk; this covers the ones the
 * phone has never downloaded. Longest model name contained in the parent's
 * name wins ("Qidi X-Max 3 0.4 nozzle" matches "X-Max 3" from Qidi).
 */
export function guessPrinterFor(
  inherits: string,
  catalogue: AvailablePrinter[],
): AvailablePrinter | null {
  const haystack = inherits.toLowerCase().replace(/\s+/g, ' ')
  let best: AvailablePrinter | null = null
  for (const p of catalogue) {
    const needle = p.model.toLowerCase().replace(/\s+/g, ' ')
    if (needle.length < 3 || !haystack.includes(needle)) continue
    if (best === null || needle.length > best.model.length) best = p
  }
  return best
}

function confirm(
  title: string,
  message: string,
  okLabel: string,
  destructive = false,
): Promise<boolean> {
  return new Promise((resolve) => {
    Alert.alert(title, message, [
      { text: 'Cancel', style: 'cancel', onPress: () => resolve(false) },
      {
        text: okLabel,
        style: destructive ? 'destructive' : 'default',
        onPress: () => resolve(true),
      },
    ])
  })
}

/** Runs a vendor install through the downloads store so the header pill shows it, and waits for it. */
async function installAndWait(printer: AvailablePrinter, ctx: ImportContext): Promise<boolean> {
  const download = useDownloadsStore
    .getState()
    .install(printer, { session: ctx.session, reloadPresets: ctx.reloadPresets })
  await download.finished
  return useDownloadsStore.getState().byId[download.id]?.state === 'done'
}

/**
 * Imports one preset file and talks the user through whatever the core could
 * not do on its own. Resolves to the final result (after any retries).
 */
export async function importPresetFile(
  path: string,
  ctx: ImportContext,
): Promise<ImportPresetsResult | null> {
  let result: ImportPresetsResult
  try {
    result = await ctx.session.importPresets(path, false, false)
  } catch (e) {
    showToast(`Import failed: ${String(e)}`, 'error')
    return null
  }

  // Same-name user presets: one question for the whole file, like the desktop.
  if (result.conflicts.length > 0) {
    const names =
      result.conflicts.slice(0, 5).join(', ') + (result.conflicts.length > 5 ? ', …' : '')
    const replace = await confirm(
      result.conflicts.length === 1
        ? 'Replace preset?'
        : `Replace ${result.conflicts.length} presets?`,
      `You already have ${names}. Replace with the copy from this file?`,
      'Replace',
      true,
    )
    if (replace) {
      try {
        result = await ctx.session.importPresets(path, true, false)
      } catch (e) {
        showToast(`Import failed: ${String(e)}`, 'error')
        return null
      }
    }
  }

  // A parent that lives in a vendor profile the phone does not have: offer to
  // fetch it, then run the import again. One detour only.
  const orphans = result.skipped.filter((s) => s.reason === 'missing_parent')
  if (orphans.length > 0) {
    let target: AvailablePrinter | null = null
    try {
      const catalogue = await queryClient.fetchQuery({
        queryKey: queryKeys.availablePrinters,
        queryFn: () => availablePrinters(),
        staleTime: 60 * 60 * 1000,
      })
      const vendor = missingVendorIn(result)
      for (const o of orphans) {
        const guess = guessPrinterFor(
          o.inherits,
          vendor === null ? catalogue : catalogue.filter((p) => p.vendor === vendor),
        )
        if (guess !== null) {
          target = guess
          break
        }
      }
      if (target === null && vendor !== null)
        target = catalogue.find((p) => p.vendor === vendor) ?? null
    } catch {
      /* offline: fall through to the plain explanation */
    }
    if (target !== null) {
      const hint = installedVendors().includes(target.vendor)
        ? `Your ${target.vendorName} profiles do not include ${orphans[0]!.inherits}. Download the full ${target.vendorName} profile set and try again?`
        : `These presets build on ${target.vendorName} profiles that are not installed. Download ${target.model} from ${target.vendorName} and try again?`
      if (await confirm('Install printer profiles?', hint, 'Download')) {
        let ok = await installAndWait(target, ctx)
        // A model-scoped download may still lack the exact parent; fall back to the whole vendor.
        if (ok) {
          try {
            result = await ctx.session.importPresets(path, true, false)
          } catch {
            /* reported below */
          }
        }
        if (ok && result.skipped.some((s) => s.reason === 'missing_parent')) {
          try {
            await installVendor(target.vendor)
            await ctx.reloadPresets()
            result = await ctx.session.importPresets(path, true, false)
          } catch (e) {
            ok = false
            showToast(`Could not install ${target.vendorName}: ${String(e)}`, 'error')
          }
        }
      }
    }
  }

  if (result.imported.length > 0) {
    await ctx.reloadPresets()
    const printers = result.imported.filter((p) => p.kind === 'printer')
    if (printers.length === 1) {
      try {
        ctx.session.selectPreset('printer', printers[0]!.name)
        writeRememberedPresets({ printer: printers[0]!.name })
      } catch {
        /* selection is optional */
      }
    }
    ctx.onChanged?.()
    showToast(describeImported(result.imported), 'success')
  }
  const notes = describeSkipped(result.skipped)
  if (notes.length > 0) {
    if (result.imported.length === 0) {
      Alert.alert('Nothing imported', notes.join('\n\n'))
    } else {
      showToast(notes[0]!, 'info')
    }
  }
  return result
}

/** Opens the document picker on preset files and imports the choice. */
export async function pickAndImportPresets(ctx: ImportContext): Promise<void> {
  const picked = await DocumentPicker.getDocumentAsync({
    type: [
      'application/json',
      'application/zip',
      PRESET_UTIS.printer,
      PRESET_UTIS.filament,
      PRESET_UTIS.bundle,
    ],
    // iCloud and "Open in" hand us security-scoped URLs; a copy in Caches is always readable.
    copyToCacheDirectory: true,
    multiple: false,
  })
  const asset = picked.assets?.[0]
  if (picked.canceled || asset === undefined) return
  const path = decodeURIComponent(asset.uri.replace(/^file:\/\//, ''))
  await importPresetFile(path, ctx)
  try {
    new File(path).delete()
  } catch {
    /* the cache copy; best effort */
  }
}

/** Safe file name for a preset: the desktop keeps spaces, so only path separators go. */
export function archiveFileName(presetName: string, extension: string): string {
  return `${presetName.replace(/[/\\:]/g, '-')}.${extension}`
}

/**
 * Writes the presets as a desktop-compatible archive and opens the share sheet.
 * One printer plus anything else becomes an .orca_printer; any other mix is an
 * .orca_bundle. Resolves to the path written, or null when nothing was shared.
 */
export async function exportPresetBundle(
  session: OrcaSession,
  presets: PresetRef[],
  title = 'Export presets',
): Promise<string | null> {
  if (presets.length === 0) return null
  const printers = presets.filter((p) => p.kind === 'printer')
  const extension = printers.length === 1 ? 'orca_printer' : 'orca_bundle'
  const name =
    printers.length === 1
      ? printers[0]!.name
      : presets.length === 1
        ? presets[0]!.name
        : 'OrcaSlicer presets'
  const dir = new Directory(Paths.cache, 'exports')
  if (!dir.exists) dir.create({ intermediates: true })
  const target = new File(dir, archiveFileName(name, extension))
  if (target.exists) target.delete()
  let written: string
  try {
    // File.uri is percent-encoded; the core wants the plain path.
    written = await session.exportPresets(
      presets,
      decodeURIComponent(target.uri.replace(/^file:\/\//, '')),
    )
  } catch (e) {
    showToast(`Export failed: ${String(e)}`, 'error')
    return null
  }
  if (await Sharing.isAvailableAsync()) {
    await Sharing.shareAsync(new File(written).uri, {
      mimeType: 'application/zip',
      dialogTitle: title,
      UTI: extension === 'orca_printer' ? PRESET_UTIS.printer : PRESET_UTIS.bundle,
    })
  } else {
    showToast(`Saved ${target.name}`, 'success')
  }
  return written
}
