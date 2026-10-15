# /// script
# requires-python = ">=3.12"
#
# [tool.orcaslicer.plugin]
# id = "preset-update-demo"
# name = "Preset Update Demo"
# description = "Edits any setting of the currently edited process, printer, or filament preset through the preset host API."
# author = "OrcaSlicer contributors"
# version = "0.2.0"
# type = "script"
# ///
"""Preset Update Demo — edit any setting of the current preset from a plugin window.

Install this file in the local ``orca_plugins`` directory, enable the plugin, then run
"Preset Update Demo" from the Plugins dialog. The window lists every setting of the
preset the matching settings tab is editing right now — process, printer, or filament —
with its current serialized value, and writes a value back through
``orca.host.preset.<type>.update(...)``.

Process and printer each have one edited preset, taken from
``current_process_preset()`` / ``current_printer_preset()``. Filament presets are
addressed by slot instead: the window reads one preset per slot from
``current_filament_presets()`` (slot numbers are one-based, matching Orca's labels),
shows the selected slot's preset, and writes through
``orca.host.preset.filament.update(slot, key, value)``.

Any assigned slot can be edited: the backend selects that slot's preset through Orca's
normal preset selection before it writes, so applying can switch the filament tab and Orca
may ask about unsaved changes first. Only an empty slot, whose mount resolves to no preset,
is unavailable — the window says so instead of offering an edit. A slot whose preset is not
the one the filament settings already have open gets an informational note that applying
will switch to it; it is never presented as a refusal. Two slots that mount the same preset
are one preset, so editing through either of them changes both; the window marks those
slots and says which they are.

Every row is one ``config_keys()`` entry carrying ``config_value(key)``.

What ``update()`` does, and what it does not:

  * it runs the same validation and UI update path as editing the setting by hand, so
    the change lands in the preset the tab is editing and is never written to disk here
    — the preset shows as modified in OrcaSlicer once it differs from the saved one;
  * it accepts only settings of that preset category. A key that belongs to another
    category, or a value the setting would reject, raises an error and writes nothing;
  * it is refused when the selected printer's technology does not use that tab (the
    print and filament tabs do nothing under SLA), so this demo covers the three FFF
    categories only;
  * a setting whose page has not been built yet still runs its owning settings group's
    checks and callbacks; behavior that requires an actual widget (such as widget-specific
    parsing or clamping) only runs when the field exists;
  * on a filament slot it selects that slot's preset first, so Orca may prompt about
    unsaved changes in the filament tab before the value is written.

Values are OrcaSlicer's serialized config syntax — the same text a preset file stores —
so numbers are plain, percentages keep their ``%``, booleans are ``1``/``0``, choices use
the setting's internal key, and lists are semicolon-separated.
"""

import orca


# The three preset categories the main tabs edit. Each key is both the value the page
# sends and the attribute name on orca.host.preset that carries the matching editor.
KINDS = (
    {"kind": "process", "label": "Process"},
    {"kind": "printer", "label": "Printer"},
    {"kind": "filament", "label": "Filament"},
)
KIND_KEYS = {item["kind"] for item in KINDS}


def _editor(kind):
    """The host.PresetEditor for a category (orca.host.preset.process / printer).

    Filament is not reached through this helper: it is addressed by slot, so it calls
    orca.host.preset.filament.update(slot, key, value) directly.
    """
    return getattr(orca.host.preset, kind)


def _current_preset(bundle, kind):
    """The single preset the matching settings tab edits right now, as a host.Preset.

    Only the process and printer categories have one such preset; filament presets are
    addressed by slot instead (see _slot_presets).
    """
    if kind == "process":
        return bundle.current_process_preset()
    return bundle.current_printer_preset()


def _slot_presets(bundle):
    """The filament preset mounted in each slot, in slot order; a slot may be None."""
    return list(bundle.current_filament_presets())


def _preset_is_open(preset, edited):
    """Whether this slot's preset is the one the filament settings already have open.

    A slot with an assigned preset can be edited either way: applying to another slot makes
    the backend select that slot's preset first. This only distinguishes the slot that
    switches nothing from the ones that will switch the filament tab.
    """
    return preset is not None and preset.name == edited.name


def _filament_slots(presets, edited):
    """One entry per mounted slot: its one-based number, preset name, and open flag.

    The number is the slot label Orca shows; the name is None for a slot that mounts no
    preset, which is the only kind that cannot be edited.
    """
    return [
        {
            "slot": index + 1,
            "name": None if preset is None else preset.name,
            "open": _preset_is_open(preset, edited),
        }
        for index, preset in enumerate(presets)
    ]


def _as_slot(value):
    """A requested slot as an int, or None when it is not a whole number."""
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, float) and value.is_integer():
        return int(value)
    return None


def _clamp_slot(slot, count):
    """The slot to display: the requested one, kept inside the available range."""
    if count <= 0:
        return 0
    number = _as_slot(slot)
    if number is None or number < 1:
        return 1
    return number if number <= count else count


def _parse_slot(slot, count):
    """Validate a requested filament slot against the mounted slots (one-based)."""
    if count == 0:
        raise RuntimeError("No filament slots are mounted")
    number = _as_slot(slot)
    if number is None:
        raise RuntimeError("A filament slot number is required")
    if number < 1 or number > count:
        raise RuntimeError("Filament slot %d is out of range (1-%d)" % (number, count))
    return number


def _rows(preset):
    """Every setting of the preset, sorted by key, as [{k, v}].

    config_value() returns None for a key the preset does not carry; the page shows
    those as empty rather than "None".
    """
    rows = []
    for key in sorted(preset.config_keys()):
        value = preset.config_value(key)
        rows.append({"k": key, "v": "" if value is None else str(value)})
    return rows


def _state(bundle, kind, slot, message="", error=""):
    """The payload the page renders: shown preset, rows, slots, result line."""
    payload = {"command": "state", "kind": kind, "message": message, "error": error}
    if kind == "filament":
        presets = _slot_presets(bundle)
        edited = bundle.filaments.edited_preset()
        effective = _clamp_slot(slot, len(presets))
        preset = None if effective == 0 else presets[effective - 1]
        payload["slots"] = _filament_slots(presets, edited)
        payload["slot"] = effective
        # An empty slot has nothing to edit; any assigned slot is editable, and one that is
        # not already open makes the backend select it first.
        payload["assigned"] = preset is not None
        payload["switches"] = preset is not None and not _preset_is_open(preset, edited)
        payload["edited_name"] = edited.name
    else:
        preset = _current_preset(bundle, kind)
    payload["preset"] = None if preset is None else {
        "name": preset.name,
        "is_dirty": bool(preset.is_dirty),
        "is_system": bool(preset.is_system),
    }
    payload["rows"] = [] if preset is None else _rows(preset)
    return payload


def _apply(kind, key, value, slot=None):
    """Set one setting of the preset the page is showing, through the host API.

    A filament preset is addressed by slot number; process and printer each resolve
    their single edited preset themselves, so no slot is passed to them.
    """
    if kind == "filament":
        orca.host.preset.filament.update(slot, key, value)
        return
    _editor(kind).update(key, value)


PAGE = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<style>
  :root {
    --bg:        var(--orca-bg, #fafafa);
    --fg:        var(--orca-fg, #1b1e23);
    --muted:     var(--orca-muted, #6b7280);
    --border:    var(--orca-border, #dadee3);
    --accent:    var(--orca-accent, #0e7c86);
    --accent-fg: var(--orca-accent-fg, #ffffff);
    --ui:        var(--orca-font, system-ui, -apple-system, 'Segoe UI', Roboto, sans-serif);
    --mono:      ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
    --surface:   rgba(127, 127, 127, .09);
  }
  @media (prefers-color-scheme: dark) {
    :root {
      --bg:     var(--orca-bg, #23262a);
      --fg:     var(--orca-fg, #e6e8ea);
      --muted:  var(--orca-muted, #9aa1a9);
      --border: var(--orca-border, #383c42);
    }
  }
  * { box-sizing: border-box; }
  html, body { height: 100%; }
  body {
    margin: 0; background: var(--bg); color: var(--fg);
    font: 13px/1.45 var(--ui);
    display: flex; flex-direction: column; overflow: hidden;
  }
  button { font: inherit; cursor: pointer; }
  :focus-visible { outline: 2px solid var(--accent); outline-offset: 1px; }

  /* header ------------------------------------------------------------- */
  header {
    flex: none; display: flex; align-items: center; gap: 10px;
    padding: 9px 14px; border-bottom: 1px solid var(--border);
  }
  .logo { color: var(--accent); font-size: 15px; line-height: 1; }
  h1 { margin: 0; font-size: 14px; font-weight: 650; letter-spacing: .01em; }

  .seg {
    display: flex; gap: 2px; margin-left: auto; padding: 2px;
    border: 1px solid var(--border); border-radius: 9px; background: var(--surface);
  }
  .seg button {
    border: 0; background: transparent; color: var(--muted);
    padding: 4px 13px; border-radius: 7px;
    transition: background .15s ease, color .15s ease;
  }
  .seg button:hover { color: var(--fg); }
  .seg button[aria-pressed="true"] { background: var(--accent); color: var(--accent-fg); }

  .iconbtn {
    width: 28px; height: 28px; border: 1px solid var(--border); border-radius: 7px;
    background: transparent; color: var(--fg); display: grid; place-items: center;
    font-size: 13px; transition: background .15s ease;
  }
  .iconbtn:hover:enabled { background: var(--surface); }
  .iconbtn:disabled { opacity: .5; }

  /* preset bar --------------------------------------------------------- */
  .presetbar {
    flex: none; display: flex; align-items: center; gap: 8px;
    padding: 7px 14px; border-bottom: 1px solid var(--border); font-size: 12px;
  }
  .presetbar .name {
    font-weight: 600; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
    max-width: 60%;
  }
  .spacer { flex: 1; }
  .tag {
    border: 1px solid var(--border); color: var(--muted); border-radius: 999px;
    padding: 1px 8px; font-size: 11px; white-space: nowrap;
  }
  .tag.on { border-color: transparent; background: var(--accent); color: var(--accent-fg); }

  /* filament slots ----------------------------------------------------- */
  .slotbar { flex: none; padding: 7px 14px 8px; border-bottom: 1px solid var(--border); }
  .slotrow { display: flex; align-items: center; gap: 9px; min-width: 0; }
  .slotlabel {
    flex: none; font-size: 11px; letter-spacing: .06em; text-transform: uppercase;
    color: var(--muted);
  }
  .slots { flex: 1; min-width: 0; display: flex; flex-wrap: wrap; gap: 6px;
           max-height: 84px; overflow-y: auto; }
  .chip {
    display: inline-flex; align-items: center; gap: 6px; flex: none; max-width: 190px;
    padding: 2px 10px 2px 3px; border: 1px solid var(--border); border-radius: 999px;
    background: transparent; color: var(--fg);
    transition: background .15s ease, color .15s ease, border-color .15s ease;
  }
  .chip:hover { background: var(--surface); }
  .chip .n {
    flex: none; min-width: 19px; height: 19px; padding: 0 4px; border-radius: 999px;
    display: grid; place-items: center; background: var(--surface); color: var(--muted);
    font-size: 11px; font-variant-numeric: tabular-nums;
  }
  /* the slot whose preset the filament settings already have open, so applying switches nothing */
  .chip.open .n { box-shadow: inset 0 0 0 1px var(--accent); color: var(--fg); }
  .chip .s {
    min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
    font-size: 11px; color: var(--muted);
  }
  .chip[aria-pressed="true"] { background: var(--accent); border-color: var(--accent); color: var(--accent-fg); }
  .chip[aria-pressed="true"] .n {
    background: rgba(255, 255, 255, .22); color: var(--accent-fg); box-shadow: none;
  }
  .chip[aria-pressed="true"] .s { color: var(--accent-fg); }
  /* a slot that mounts the preset the page is showing: editing either changes both */
  .chip.linked::after {
    content: ""; flex: none; width: 5px; height: 5px;
    border-radius: 50%; background: var(--accent);
  }
  .slotnote {
    margin-top: 7px; padding-left: 8px; border-left: 2px solid var(--accent);
    font-size: 11px; line-height: 1.5; color: var(--muted);
  }
  .slotnote:empty { display: none; }

  /* panes -------------------------------------------------------------- */
  main { flex: 1; display: flex; min-height: 0; }

  .list-pane {
    flex: none; width: 45%; min-width: 250px; min-height: 0;
    display: flex; flex-direction: column; border-right: 1px solid var(--border);
  }
  .search {
    flex: none; display: flex; align-items: center; gap: 8px;
    padding: 8px 10px; border-bottom: 1px solid var(--border);
  }
  .search input {
    flex: 1; min-width: 0; font: inherit; color: var(--fg); background: var(--bg);
    border: 1px solid var(--border); border-radius: 7px; padding: 4px 9px;
  }
  .rows { flex: 1; min-height: 0; overflow-y: auto; padding: 3px 0; }

  .row {
    display: flex; align-items: baseline; gap: 8px; width: 100%;
    padding: 5px 10px; text-align: left; border: 0; border-left: 3px solid transparent;
    background: transparent; color: var(--fg);
  }
  .row:hover { background: var(--surface); }
  .row[aria-selected="true"] { background: var(--surface); border-left-color: var(--accent); }
  .row .k {
    flex: 1; min-width: 0; font-family: var(--mono); font-size: 12px;
    overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  }
  .row .v {
    flex: none; max-width: 40%; font-family: var(--mono); font-size: 11px;
    color: var(--muted); overflow: hidden; text-overflow: ellipsis; white-space: nowrap;
  }
  .row .dot {
    flex: none; align-self: center; width: 6px; height: 6px; border-radius: 50%;
    background: var(--accent); visibility: hidden;
  }
  .row.changed .dot { visibility: visible; }
  @keyframes flash {
    0%   { box-shadow: inset 0 0 0 2px var(--accent); }
    100% { box-shadow: inset 0 0 0 2px transparent; }
  }
  .row.flash { animation: flash .7s ease-out; }
  @media (prefers-reduced-motion: reduce) {
    .row.flash { animation: none; }
  }

  .empty { color: var(--muted); padding: 14px 12px; }
  .nil { font-style: italic; opacity: .75; }

  /* editor ------------------------------------------------------------- */
  .edit-pane {
    flex: 1; min-width: 0; display: flex; flex-direction: column; gap: 9px;
    padding: 12px 14px; overflow-y: auto;
  }
  .edit-key { font-family: var(--mono); font-size: 13px; font-weight: 600; overflow-wrap: anywhere; }
  .lbl { font-size: 11px; letter-spacing: .06em; text-transform: uppercase; color: var(--muted); }
  .current {
    margin: 0; max-height: 108px; overflow: auto;
    font-family: var(--mono); font-size: 12px;
    background: var(--surface); border: 1px solid var(--border); border-radius: 7px;
    padding: 7px 10px; white-space: pre-wrap; overflow-wrap: anywhere;
  }
  textarea {
    width: 100%; min-height: 74px; resize: vertical;
    font-family: var(--mono); font-size: 12px; color: var(--fg); background: var(--bg);
    border: 1px solid var(--border); border-radius: 7px; padding: 7px 10px;
  }
  .actions { display: flex; gap: 8px; align-items: center; }
  button.primary {
    padding: 5px 14px; border-radius: 7px; border: 1px solid var(--accent);
    background: var(--accent); color: var(--accent-fg);
  }
  button.ghost {
    padding: 5px 12px; border-radius: 7px; border: 1px solid var(--border);
    background: transparent; color: var(--fg);
  }
  button.primary:disabled, button.ghost:disabled { opacity: .5; cursor: default; }
  button.primary:hover:enabled { filter: brightness(1.08); }
  button.ghost:hover:enabled { background: var(--surface); }

  .notice {
    padding: 8px 11px; border: 1px solid var(--border); border-left: 3px solid var(--accent);
    border-radius: 7px; background: var(--surface);
    font-size: 12px; line-height: 1.5; color: var(--muted);
  }
  .notice b { color: var(--fg); font-weight: 600; }
  .notice:empty { display: none; }

  .syntax {
    margin-top: 2px; padding: 9px 11px; border: 1px solid var(--border);
    border-left: 3px solid var(--accent); border-radius: 7px;
    font-size: 12px; line-height: 1.55; color: var(--muted);
  }
  .syntax b { color: var(--fg); font-weight: 600; }
  .syntax code { font-family: var(--mono); font-size: 11.5px; color: var(--fg); }

  /* status ------------------------------------------------------------- */
  footer {
    flex: none; min-height: 30px; padding: 7px 14px;
    border-top: 1px solid var(--border); font-size: 12px; color: var(--muted);
  }
  footer.ok { color: #0f8a5f; }
  footer.err { color: #c8382c; }
</style>
</head>
<body>
  <header>
    <span class="logo">◈</span>
    <h1>Preset update</h1>
    <div class="seg" id="seg"></div>
    <button class="iconbtn" id="refresh" title="Reload the current values">⟳</button>
    <button class="iconbtn" id="close" title="Close">✕</button>
  </header>

  <div class="presetbar">
    <span class="name" id="pname">Loading…</span>
    <span id="ptag"></span>
    <span class="spacer"></span>
    <span class="tag" id="pcount">0 settings</span>
  </div>

  <div class="slotbar" id="slotbar" hidden>
    <div class="slotrow">
      <span class="slotlabel">Filament slots</span>
      <div class="slots" id="slots"></div>
    </div>
    <div class="slotnote" id="slotnote"></div>
  </div>

  <main>
    <section class="list-pane">
      <div class="search">
        <input id="q" type="search" placeholder="Search settings…" autocomplete="off" spellcheck="false">
        <span class="tag" id="qcount">0</span>
      </div>
      <div class="rows" id="rows"></div>
    </section>

    <section class="edit-pane">
      <div class="edit-key" id="ekey">Select a setting</div>
      <div class="lbl">Current value</div>
      <pre class="current" id="ecur">—</pre>
      <label class="lbl" for="evalue">New value</label>
      <textarea id="evalue" spellcheck="false" autocomplete="off"
                placeholder="Serialized value" disabled></textarea>
      <div class="notice" id="notice"></div>
      <div class="actions">
        <button class="primary" id="apply" disabled>Apply</button>
        <button class="ghost" id="reset" disabled>Reset to current</button>
      </div>
      <div class="syntax">
        <b>Values use OrcaSlicer's serialized config syntax</b> — the same text a preset
        file stores. Numbers are plain (<code>0.2</code>, <code>3</code>) and percentages keep
        their sign (<code>15%</code>). On/off is <code>1</code> or <code>0</code>. A choice uses
        the setting's own key, e.g. <code>grid</code>. Lists are semicolon-separated
        (<code>0.4;0.4;0.4</code>, or <code>"#FF0000";"#00FF00"</code>). Text keeps its
        escapes, so a line break stays <code>\n</code>. A key that does not belong to this
        preset, or a value the setting would reject, comes back as an error and writes
        nothing. The edit lands in the preset the tab is editing and stays unsaved.
      </div>
    </section>
  </main>

  <footer id="status"></footer>

<script>
'use strict';

const SEG = [['process', 'Process'], ['printer', 'Printer'], ['filament', 'Filament']];
const IDLE = 'Edits apply to the preset shown here and stay unsaved until you save '
  + 'or export it.';

const S = {
  kind: 'process',
  slot: 1,              // selected filament slot (one-based); ignored by the other kinds
  slots: [],            // [{slot, name, open}] from current_filament_presets()
  preset: null,
  rows: [],
  selected: null,
  changed: {},          // "kind/preset/key" -> true, for edits made in this window
  pending: null,        // {kind, slot, preset, key} of an apply awaiting its reply
  error: '',            // the last error the plugin reported, if any
  assigned: true,       // filament only: the shown slot mounts a preset, so it is editable
  switches: false,      // filament only: applying will select that preset in the filament tab
  editedName: null,     // filament only: the preset the filament settings have open
  busy: true,
  filter: '',
  statusText: '',
  statusCls: '',
};

const $ = id => document.getElementById(id);
const esc = s => String(s == null ? '' : s)
  .replace(/[&<>"']/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));

/* ------------------------------------------------------------------ paint */
function paint() {
  paintSeg();
  const p = S.preset;
  const slotTag = S.kind === 'filament' && S.slot
    ? '<span class="tag">slot ' + S.slot + '</span>' : '';
  $('pname').textContent = p ? p.name : (S.busy ? 'Loading…' : 'No preset');
  $('pname').title = p ? p.name : '';
  $('ptag').innerHTML = slotTag + (p
    ? '<span class="tag">' + (p.is_system ? 'system' : 'user') + '</span>'
      + (p.is_dirty ? '<span class="tag on">modified</span>' : '<span class="tag">saved</span>')
    : '');
  $('pcount').textContent = S.rows.length + (S.rows.length === 1 ? ' setting' : ' settings');
  $('refresh').disabled = S.busy;
  paintSlots();
  paintNotice();
  renderList();
  paintEditor();
  paintStatus();
}

function paintSeg() {
  // Built once (see buildSeg) so a click never loses focus; only the state is repainted.
  document.querySelectorAll('#seg button').forEach(b =>
    b.setAttribute('aria-pressed', String(b.dataset.kind === S.kind)));
}

/* the filament slot strip: one chip per mounted slot, shown for filament only */
function paintSlots() {
  const show = S.kind === 'filament' && (S.slots.length > 0 || S.busy);
  $('slotbar').hidden = !show;
  if (!show) { $('slots').innerHTML = ''; $('slotnote').innerHTML = ''; return; }
  // Rebuilding the chips drops keyboard focus; put it back on the same slot's chip.
  const focused = document.activeElement && document.activeElement.dataset
    ? document.activeElement.dataset.slot : null;
  $('slots').innerHTML = S.slots.map(s => {
    const selected = s.slot === S.slot;
    const linked = !selected && sharesSelected(s.name);
    const label = s.name == null ? '<span class="nil">empty slot</span>' : esc(s.name);
    const hint = s.name ? ': ' + s.name : ': no preset';
    return '<button type="button" class="chip' + (linked ? ' linked' : '')
      + (s.open ? ' open' : '') + '"'
      + ' data-slot="' + s.slot + '" aria-pressed="' + selected + '"'
      + ' title="' + esc('Filament slot ' + s.slot + hint
                         + (s.open ? ' — open in the filament settings' : '')) + '">'
      + '<span class="n">' + s.slot + '</span><span class="s">' + label + '</span></button>';
  }).join('');
  $('slotnote').innerHTML = shareNote();
  if (focused) {
    const chip = document.querySelector('#slots .chip[data-slot="' + focused + '"]');
    if (chip) chip.focus();
  }
}

function slotName(slot) {
  const found = S.slots.find(s => s.slot === slot);
  return found && found.name ? found.name : null;
}

// Applying selects the slot's preset first, so say when that switches the filament tab. An
// empty slot is the only unavailable one, and it says so instead of offering an edit.
function paintNotice() {
  const el = $('notice');
  if (S.kind !== 'filament' || S.busy) { el.innerHTML = ''; return; }
  // With nothing mounted the list already says so; avoid naming a slot number here.
  if (!S.slots.length) { el.innerHTML = ''; return; }
  const name = S.preset ? S.preset.name : null;
  if (!name) {
    el.innerHTML = 'Slot ' + S.slot + ' holds no filament preset. Assign one in the '
      + 'OrcaSlicer filament settings first.';
    return;
  }
  if (!S.switches) { el.innerHTML = ''; return; }
  const open = S.editedName
    ? ', while the filament settings have <b>' + esc(S.editedName) + '</b> open'
    : '';
  el.innerHTML = 'Slot ' + S.slot + ' uses <b>' + esc(name) + '</b>' + open
    + '. Applying switches the filament tab to <b>' + esc(name) + '</b> first — Orca may '
    + 'ask about unsaved changes there — then sets the value.';
}

function sharesSelected(name) {
  const current = slotName(S.slot);
  return !!name && !!current && name === current;
}

function listSlots(numbers) {
  if (numbers.length === 1) return String(numbers[0]);
  return numbers.slice(0, -1).join(', ') + ' and ' + numbers[numbers.length - 1];
}

// Slots mounting the same preset are one preset, so say which slots an edit reaches.
function shareNote() {
  const name = slotName(S.slot);
  if (!name) return '';
  const sharing = S.slots.filter(s => s.name === name).map(s => s.slot);
  if (sharing.length < 2) return '';
  return 'Slots ' + listSlots(sharing) + ' mount the same filament preset, so an edit '
    + 'here changes every one of them.';
}

function changedKey(presetName, key) {
  return S.kind + '/' + (presetName || '?') + '/' + key;
}

function paintStatus() {
  const el = $('status');
  if (S.statusText) {
    el.textContent = S.statusText;
    el.className = S.statusCls;
  } else if (S.busy) {
    el.textContent = 'Working…';
    el.className = '';
  } else {
    el.textContent = IDLE;
    el.className = '';
  }
}

function setStatus(text, cls) {
  S.statusText = text || '';
  S.statusCls = cls || '';
  paintStatus();
}

function visibleRows() {
  const q = S.filter.trim().toLowerCase();
  if (!q) return S.rows;
  return S.rows.filter(r => r.k.toLowerCase().includes(q) || r.v.toLowerCase().includes(q));
}

function renderList() {
  const scroller = $('rows');
  const top = scroller.scrollTop;
  $('qcount').textContent = String(visibleRows().length);

  if (!S.rows.length) {
    let text = 'This preset reports no settings.';
    if (S.busy) text = 'Loading settings…';
    else if (S.error) text = 'These values could not be read — see the message below.';
    else if (S.kind === 'filament' && !S.slots.length) text = 'No filament slots are mounted.';
    else if (S.kind === 'filament' && !S.preset) text = 'This filament slot has no preset.';
    scroller.innerHTML = '<div class="empty">' + text + '</div>';
    return;
  }
  const rows = visibleRows();
  if (!rows.length) {
    scroller.innerHTML = '<div class="empty">No setting matches “' + esc(S.filter) + '”.</div>';
    return;
  }
  scroller.innerHTML = rows.map(r => {
    const value = r.v === '' ? '<span class="nil">empty</span>' : esc(r.v);
    return '<button type="button" class="row'
      + (S.changed[changedKey(S.preset && S.preset.name, r.k)] ? ' changed' : '') + '"'
      + ' data-key="' + esc(r.k) + '"'
      + ' aria-selected="' + (r.k === S.selected) + '"'
      + ' title="' + esc(r.k + ' = ' + r.v) + '">'
      + '<span class="k">' + esc(r.k) + '</span>'
      + '<span class="v">' + value + '</span>'
      + '<span class="dot"></span></button>';
  }).join('');
  scroller.scrollTop = top;
}

function paintEditor() {
  const row = S.rows.find(r => r.k === S.selected) || null;
  // Every assigned slot is editable now that applying selects the slot's preset first, so
  // Apply is off only for an empty slot, a missing selection, or a request in flight.
  const selected = !!row && !S.busy;
  $('ekey').textContent = row ? row.k : 'Select a setting';
  $('ecur').textContent = row ? (row.v === '' ? '(empty)' : row.v) : '—';
  $('evalue').disabled = !selected;
  $('apply').disabled = !selected || !S.assigned;
  $('reset').disabled = !selected;
}

/* --------------------------------------------------------------- actions */
function buildSeg() {
  $('seg').innerHTML = SEG.map(([kind, label]) =>
    '<button type="button" data-kind="' + kind + '">' + label + '</button>').join('');
}

function selectKey(key) {
  S.selected = key;
  const row = S.rows.find(r => r.k === key);
  $('evalue').value = row ? row.v : '';
  // Repaint the rows in place so the scroll position of a long list is kept.
  document.querySelectorAll('#rows .row').forEach(el =>
    el.setAttribute('aria-selected', String(el.dataset.key === key)));
  paintEditor();
}

function switchKind(kind) {
  S.kind = kind;
  S.selected = null;
  S.pending = null;
  S.preset = null;
  S.rows = [];
  S.busy = true;
  $('evalue').value = '';
  setStatus('', '');
  paint();
  orca.postMessage({command: 'state', kind: kind, slot: S.slot});
}

// Two slots that mount the same preset show the same settings, so only a switch to a
// different preset drops the current selection and the value being typed.
function selectSlot(slot) {
  if (S.busy || slot === S.slot) return;
  const same = !!slotName(slot) && slotName(slot) === slotName(S.slot);
  S.slot = slot;
  S.pending = null;
  S.busy = true;
  if (!same) {
    S.preset = null;
    S.rows = [];
    S.selected = null;
    $('evalue').value = '';
  }
  setStatus('', '');
  paint();
  orca.postMessage({command: 'state', kind: S.kind, slot: slot});
}

function reload() {
  if (S.busy) return;
  S.busy = true;
  S.pending = null;
  setStatus('', '');
  paint();
  orca.postMessage({command: 'state', kind: S.kind, slot: S.slot});
}

function applyValue() {
  const row = S.rows.find(r => r.k === S.selected);
  // Mirror the Apply button: only a missing selection, a request in flight, or an empty
  // slot blocks an edit, so Enter cannot send one the button itself would refuse.
  if (!row || S.busy || !S.assigned) return;
  S.pending = {
    kind: S.kind, slot: S.slot,
    preset: S.preset ? S.preset.name : null, key: row.k,
  };
  S.busy = true;
  setStatus('', '');
  paint();
  orca.postMessage({command: 'apply', kind: S.kind, slot: S.slot, key: row.k,
                    value: $('evalue').value});
}

function flashKey(key) {
  document.querySelectorAll('#rows .row').forEach(el => {
    if (el.dataset.key !== key) return;
    el.classList.remove('flash');
    void el.offsetWidth;    // restart the animation if it ran on this row already
    el.classList.add('flash');
    el.addEventListener('animationend', () => el.classList.remove('flash'), {once: true});
  });
}

/* ----------------------------------------------------------------- wiring */
buildSeg();

$('seg').addEventListener('click', e => {
  const button = e.target.closest('[data-kind]');
  if (!button || S.busy || button.dataset.kind === S.kind) return;
  switchKind(button.dataset.kind);
});

$('rows').addEventListener('click', e => {
  const row = e.target.closest('.row');
  if (row && !S.busy) selectKey(row.dataset.key);
});

$('slots').addEventListener('click', e => {
  const chip = e.target.closest('[data-slot]');
  if (chip && !S.busy) selectSlot(Number(chip.dataset.slot));
});

$('q').addEventListener('input', e => {
  S.filter = e.target.value;
  renderList();
});

$('apply').addEventListener('click', applyValue);

$('reset').addEventListener('click', () => {
  const row = S.rows.find(r => r.k === S.selected);
  if (row) $('evalue').value = row.v;
});

$('evalue').addEventListener('keydown', e => {
  if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); applyValue(); }
  else if (e.key === 'Escape') { e.preventDefault(); $('reset').click(); }
});

$('refresh').addEventListener('click', reload);
$('close').addEventListener('click', () => orca.close());

/* -------------------------------------------------------------- messages */
orca.onMessage(msg => {
  if (!msg || msg.command !== 'state') return;
  if (msg.kind !== S.kind) return;      // a reply for a category we already left

  const pending = S.pending;
  S.busy = false;
  S.pending = null;
  S.slots = msg.slots || [];
  // Adopt the slot the plugin actually used (it clamps a slot that no longer exists).
  if (S.kind === 'filament' && typeof msg.slot === 'number') S.slot = msg.slot;
  S.preset = msg.preset || null;
  S.rows = msg.rows || [];
  S.error = msg.error || '';
  // Filament editability is a slot concern; process and printer have a single edited preset.
  S.assigned = S.kind === 'filament' ? !!msg.assigned : true;
  S.switches = S.kind === 'filament' ? !!msg.switches : false;
  S.editedName = S.kind === 'filament' ? (msg.edited_name || null) : null;
  if (S.selected && !S.rows.some(r => r.k === S.selected)) S.selected = null;

  if (msg.error) {
    // Keep the typed value so it can be corrected, and keep the current selection.
    setStatus('⚠ ' + msg.error, 'err');
  } else {
    if (pending) S.changed[changedKey(pending.preset, pending.key)] = true;
    setStatus(msg.message || '', msg.message ? 'ok' : '');
  }

  paint();

  if (!msg.error && pending) {
    const row = S.rows.find(r => r.k === pending.key);
    if (row) {                       // show the value as the preset now stores it
      $('evalue').value = row.v;
      flashKey(row.k);
    }
  }
});

paint();
orca.postMessage({command: 'state', kind: S.kind, slot: S.slot});
</script>
</body>
</html>"""


class PresetUpdateDemo(orca.script.ScriptPluginCapabilityBase):
    window = None
    kind = "process"      # the category the open window is showing, for a re-run refresh
    slot = 1              # the filament slot the open window is showing

    def get_name(self):
        return "Preset update demo"

    def execute(self):
        try:
            if self.window is not None and self.window.is_open():
                self._post_state(self.kind, self.slot)
                return orca.ExecutionResult.success("Preset update demo refreshed.")
            self.window = orca.host.ui.create_window(
                title="Preset update demo", html=PAGE, width=820, height=560,
                on_message=self.on_message, on_close=self.on_close,
            )
            return orca.ExecutionResult.success("Preset update demo opened.")
        except Exception as error:
            message = "Preset update demo failed: %s" % error
            orca.host.ui.message(message, title="Preset update demo", buttons="ok", icon="error")
            return orca.ExecutionResult.failure(orca.PluginResult.RecoverableError, message)

    def on_close(self):
        self.window = None

    def _post_state(self, kind="process", slot=1, message="", error=""):
        try:
            payload = _state(orca.host.preset_bundle(), kind, slot, message, error)
        except Exception as state_error:
            payload = {"command": "state", "kind": kind, "slot": slot, "slots": [],
                       "assigned": False, "switches": False, "edited_name": None,
                       "preset": None, "rows": [], "message": message,
                       "error": str(state_error)}
        if self.window is not None and self.window.is_open():
            self.window.post(payload)

    def on_message(self, message):
        kind = "process"
        slot = 1
        try:
            message = message or {}
            kind = message.get("kind", "process")
            if not isinstance(kind, str) or kind not in KIND_KEYS:
                raise RuntimeError("Unknown preset category %r" % (kind,))
            self.kind = kind
            slot = message.get("slot", 1)

            command = message.get("command")
            if command == "state":
                self.slot = slot
                self._post_state(kind, slot)
                return
            if command == "apply":
                key = message.get("key")
                value = message.get("value")
                if not isinstance(key, str) or not key:
                    raise RuntimeError("A setting key is required")
                if not isinstance(value, str):
                    raise RuntimeError("A value string is required")
                where = ""
                if kind == "filament":
                    presets = _slot_presets(orca.host.preset_bundle())
                    # Validate the slot strictly: an out-of-range slot must fail loudly
                    # rather than edit a different filament. A slot with no preset has
                    # nothing to write, which is the only slot shape the backend refuses.
                    slot = _parse_slot(slot, len(presets))
                    if presets[slot - 1] is None:
                        raise RuntimeError("Filament slot %d has no preset to edit" % slot)
                    _apply(kind, key, value, slot)
                    where = " on slot %d" % slot
                else:
                    _apply(kind, key, value)
                shown = value if value else "(empty)"
                self.slot = slot
                self._post_state(
                    kind, slot,
                    message='Set %s = "%s"%s. The edit stays in the preset tab until you save it.'
                            % (key, shown, where),
                )
                return
            raise RuntimeError("Unknown command %r" % (command,))
        except Exception as error:
            safe_kind = kind if isinstance(kind, str) and kind in KIND_KEYS else "process"
            self._post_state(safe_kind, slot, error=str(error))


@orca.plugin
class PresetUpdateDemoPackage(orca.base):
    def register_capabilities(self):
        orca.register_capability(PresetUpdateDemo)
