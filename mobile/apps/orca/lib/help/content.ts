// The in-app guides. One source: the Help screens render these steps, and
// `pnpm help-docs` (mobile/scripts/help-docs.mjs) turns them into the markdown
// under docs/mobile/. Images are keys into assets/help/<topic>/<file>.png, mapped
// by the generated lib/help/images.ts; a step whose image is not captured yet
// renders without one.

export type HelpTopicId = 'transfer-presets' | 'using-the-app'

export interface HelpStep {
  title: string
  body: string
  /** `<topic>/<file-without-extension>` under assets/help. */
  image?: string
  /** Alt text for the image; markdown uses it too. */
  alt?: string
}

export interface HelpSection {
  title: string
  intro?: string
  steps: HelpStep[]
}

export interface HelpTopic {
  id: HelpTopicId
  title: string
  summary: string
  sections: HelpSection[]
}

export const HELP_TOPICS: HelpTopic[] = [
  {
    id: 'transfer-presets',
    title: 'Move presets between desktop and phone',
    summary: 'Export your printer, filament and process presets from OrcaSlicer on the computer, import them here, and send changes back.',
    sections: [
      {
        title: 'Export from the desktop app',
        intro:
          'Desktop OrcaSlicer bundles a printer together with every filament and process preset you made for it into one .orca_printer file. That file is what you bring to the phone.',
        steps: [
          {
            title: 'Open the export dialog',
            body:
              'On macOS choose File ▸ Export ▸ Export Preset Bundle… from the menu bar. On Windows and Linux click the File button in the title bar, then Export ▸ Export Preset Bundle….',
            image: 'transfer-presets/01-desktop-file-export-menu',
            alt: 'The File menu with Export ▸ Export Preset Bundle… highlighted',
          },
          {
            title: 'Pick what to export',
            body:
              'Leave "Printer config bundle (.orca_printer)" selected and tick the printer you want on the phone. The bundle includes every user filament and process that is compatible with that printer. Built-in presets are not exported; save a built-in preset under your own name first if you want it included.',
            image: 'transfer-presets/02-desktop-export-dialog',
            alt: 'The Export Preset Bundle dialog with one printer ticked',
          },
          {
            title: 'Choose where to save',
            body: 'Press OK and pick a folder. Desktop writes one file per printer, named after the printer.',
            image: 'transfer-presets/03-desktop-choose-folder',
            alt: 'The folder chooser',
          },
          {
            title: 'Wait for the confirmation',
            body: 'A small "Export successful" dialog closes the export. The file is a plain zip of preset JSON files with an .orca_printer extension, so it travels like any other document.',
            image: 'transfer-presets/04-desktop-export-success',
            alt: 'The Export successful dialog',
          },
          {
            title: 'Get the file to your phone',
            body:
              'AirDrop it, save it to iCloud Drive, or attach it to a message to yourself. Printer bundles are small, usually well under a megabyte, and your printer’s network address and API key are stripped before export.',
            image: 'transfer-presets/05-desktop-finder-airdrop',
            alt: 'Finder showing the exported .orca_printer file',
          },
        ],
      },
      {
        title: 'Import on the phone',
        steps: [
          {
            title: 'Open the file',
            body:
              'Tap the .orca_printer in Files, in the AirDrop prompt or in a message, then choose Orca. You can also tap Import on the Presets card and pick the file yourself. Single preset .json files, .orca_filament bundles and the .zip exports work the same way.',
            image: 'transfer-presets/07-ios-open-in-orca',
            alt: 'The import picker showing the bundle in the app\u2019s own folder',
          },
          {
            title: 'Install the printer’s profiles if asked',
            body:
              'Your presets are stored as changes on top of the vendor’s profiles, so those profiles must be on the phone. If they are missing, Orca offers to download the right printer from the catalogue and then finishes the import.',
          },
          {
            title: 'Check the result',
            body:
              'A toast tells you what came in. The imported printer is selected for you, and its filaments and processes appear under My presets in each picker. Imported presets are ordinary user presets: edit, rename, delete and sync them like any other.',
            image: 'transfer-presets/08-ios-import-confirm',
            alt: 'The toast after an import: 1 printer, 4 filaments and 2 processes',
          },
        ],
      },
      {
        title: 'Send changes back to the desktop',
        steps: [
          {
            title: 'Export from the phone',
            body:
              'Open the preset (tap its row on the Presets card), then Export. Choose Printer bundle to get an .orca_printer with the printer plus its compatible user filaments and processes, or JSON file for just this preset. Share it to your computer.',
            image: 'transfer-presets/10-ios-export-share',
            alt: 'The Export sheet on the Edit preset screen',
          },
          {
            title: 'Import on the desktop',
            body:
              'In desktop OrcaSlicer choose File ▸ Import ▸ Import Configs…, select the file and confirm. Presets with the same name as one you already have ask before they are replaced.',
            image: 'transfer-presets/11-desktop-import-configs',
            alt: 'File ▸ Import ▸ Import Configs… on the desktop',
          },
        ],
      },
      {
        title: 'Or let Orca Cloud do it',
        intro:
          'If you sign in to the same Orca Cloud account on both, user presets sync by themselves: save on one side, see it on the other. Files are still the way to go for sharing with other people or when you do not want an account.',
        steps: [
          {
            title: 'Sign in on both',
            body:
              'Desktop: Preferences ▸ Sync user presets, then sign in from the account button. Phone: Settings ▸ Account ▸ Sign in to Orca Cloud. Both must use the same account.',
            image: 'transfer-presets/12-ios-account-sync',
            alt: 'The Account screen signed in, with Sync now and the last sync time',
          },
          {
            title: 'When things sync',
            body:
              'The phone pulls and pushes when you open the app, after you save or delete a preset, and when you tap Sync now. The desktop pushes about once a minute and pulls when it starts, so a change made on the phone shows up there after a restart or a manual sync.',
          },
        ],
      },
    ],
  },
  {
    id: 'using-the-app',
    title: 'Using Orca on iPhone and iPad',
    summary: 'From picking a printer to sending G-code, in the order you will do it.',
    sections: [
      {
        title: 'Set up',
        steps: [
          {
            title: 'Choose a printer',
            body:
              'Tap Change printer on the Presets card, search for your machine and tap Install. Only that printer’s profiles are downloaded, together with the shared filament library. Add more printers at any time from the same screen.',
            image: 'using-the-app/01-ios-choose-printer',
            alt: 'The printer catalogue grouped by manufacturer',
          },
          {
            title: 'Add a destination',
            body:
              'Open the Device pane and tap Add printer. Orca talks to Moonraker (Klipper) and OctoPrint hosts; scan the local network or type the address. Test the connection before saving.',
            image: 'using-the-app/05-ios-destinations',
            alt: 'The Printers screen with a Moonraker host',
          },
        ],
      },
      {
        title: 'Prepare',
        steps: [
          {
            title: 'Bring in a model',
            body:
              'Tap Import model on the Plate card for a .3mf, STL, OBJ or STEP from Files or iCloud, or open a file from any app and choose Orca. A .3mf project replaces the whole plate layout; other files are added to the current plate.',
            image: 'using-the-app/02-ios-prepare-pane',
            alt: 'The Prepare pane with a model on the plate',
          },
          {
            title: 'Arrange and edit',
            body:
              'Drag to orbit, two fingers to pan, pinch to zoom, double-tap to fit. Tap an object to select it; the toolbar switches between Move, Rotate and Scale, and the object sheet has lay flat, auto-orient, mirror, cut and duplicate. Arrange packs everything on the plate; the expand button opens a landscape, full-screen editor.',
            image: 'using-the-app/03-ios-object-tools',
            alt: 'The object toolbar and detail sheet',
          },
          {
            title: 'Pick presets and tweak settings',
            body:
              'The Presets card shows the printer, filament and process in use. Tap a row to switch, or tap the gear to edit values. The editor is the desktop’s parameter list with Simple, Advanced and Expert levels and a search box; changed values are marked, and Save as keeps them as your own preset.',
            image: 'using-the-app/07-ios-edit-preset',
            alt: 'The Edit preset screen with a changed value',
          },
        ],
      },
      {
        title: 'Slice and print',
        steps: [
          {
            title: 'Slice',
            body:
              'Tap Slice plate. Progress shows on the card; large models take a while on a phone. The Preview pane then shows the toolpaths with a layer slider, colouring by feature, speed or filament, plus time, filament use and cost.',
            image: 'using-the-app/04-ios-slice-result',
            alt: 'The Preview pane after slicing',
          },
          {
            title: 'Send or save the G-code',
            body:
              'Send to a destination starts the print on your Moonraker or OctoPrint host. Share saves the G-code to Files or another app. The Device pane follows the job with temperatures, progress and the camera, and a Live Activity keeps it on the Lock Screen.',
            image: 'using-the-app/06-ios-device-pane',
            alt: 'The Device pane during a print',
          },
          {
            title: 'Calibrate',
            body:
              'Calibration on the Presets card generates the desktop’s test prints: flow, pressure advance, temperature and retraction towers, with the matching settings applied to a copy of your process preset.',
            image: 'using-the-app/08-ios-calibration',
            alt: 'The Calibration screen',
          },
        ],
      },
      {
        title: 'Keep things tidy',
        steps: [
          {
            title: 'Projects and history',
            body:
              'Save project writes a .3mf with the plate, models and presets, re-openable here or on the desktop. The home screen remembers recent projects and prints, and the plate is autosaved so a force-quit does not lose your layout.',
          },
          {
            title: 'Storage',
            body:
              'Settings ▸ Storage shows what the downloaded profiles, projects and G-code take up, lets you clear each category, and can reset the first-run guide.',
            image: 'using-the-app/09-ios-storage',
            alt: 'The Storage screen',
          },
        ],
      },
    ],
  },
]

export function helpTopic(id: string | undefined): HelpTopic | undefined {
  return HELP_TOPICS.find((t) => t.id === id)
}

/** Every image key a topic references, in order. */
export function imageKeys(topic: HelpTopic): string[] {
  return topic.sections.flatMap((s) => s.steps.map((st) => st.image).filter((k): k is string => k !== undefined))
}
