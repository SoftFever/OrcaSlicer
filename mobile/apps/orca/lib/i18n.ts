import { catalogs } from '@/i18n/catalogs'

// Translates the English msgids the core exports (option labels, tooltips, categories,
// slicing status text) with the repository's gettext catalogs, reduced to the core's
// strings by mobile/scripts/i18n.mjs. The UI's own strings are not translated yet.

let active: Record<string, string> | null = null
let activeLanguage = 'en'

/** Catalog directory names available, e.g. "de", "zh_CN". */
export function availableLanguages(): string[] {
  return Object.keys(catalogs)
}

/** Picks the catalog for a BCP 47 tag ("de-AT" tries de_AT, then de). Returns the language used. */
export function setLanguage(tag: string): string {
  const [lang = 'en', region] = tag.replace('-', '_').split('_')
  const candidates = region !== undefined ? [`${lang}_${region}`, lang] : [lang]
  for (const candidate of candidates) {
    const load = catalogs[candidate]
    if (load !== undefined) {
      active = load()
      activeLanguage = candidate
      return candidate
    }
  }
  active = null
  activeLanguage = 'en'
  return 'en'
}

export function currentLanguage(): string {
  return activeLanguage
}

/** The device locale as Hermes reports it, or "en". */
export function deviceLanguage(): string {
  try {
    return Intl.DateTimeFormat().resolvedOptions().locale
  } catch {
    return 'en'
  }
}

/** Translated text for a core msgid, or the msgid itself when there is no translation. */
export function t(msgid: string): string {
  if (active === null) return msgid
  return active[msgid] ?? msgid
}
