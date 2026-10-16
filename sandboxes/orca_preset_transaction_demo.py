# /// script
# requires-python = ">=3.12"
#
# [tool.orcaslicer.plugin]
# id = "preset-transaction-demo"
# name = "Preset Transaction Demo"
# description = "Manual test for the staged begin_update()/set()/commit() preset API."
# author = "OrcaSlicer contributors"
# version = "0.1.0"
# type = "script"
# ///
"""Preset Transaction Demo — a manual test harness for the staged preset API.

Install this file in the local ``orca_plugins`` directory, enable the plugin, then run
"Preset Transaction Demo" from the Plugins dialog. The window drives
``orca.host.preset.<category>.begin_update(...)`` and shows the returned
``PresetUpdateResult``.

This is an asynchronous Script capability: ``execute`` is ``async def`` and runs on the
background Script worker's own asyncio event loop. ``begin_update()`` is awaitable — it
suspends the coroutine until the normal preset-selection / unsaved-edit dialog is answered,
without blocking Orca's UI thread. ``set()`` and ``commit()`` remain synchronous and are
only valid on that worker.

The window's own messages arrive on the UI thread, so ``on_message`` cannot await directly.
Instead the handler enqueues each message onto the worker loop (``call_soon_threadsafe``) and
``execute`` consumes them, one coroutine at a time, so every command can ``await
begin_update``. Closing the window enqueues a sentinel that ends ``execute``.

Categories
  * process / printer: target is the active preset, or the name typed into the target
    field, resolved through the normal preset-selection flow;
  * filament: target is the 1-based slot number, with an optional preset name that
    selects (and assigns) that preset for the slot.

Buttons exercise the documented outcomes:
  * "Commit" stages every key/value row and commits the batch;
  * "Duplicate key" stages the same key twice and shows the rejected conflict;
  * "Invalid key" stages a key that belongs to no preset and shows the commit error.

Nothing here saves to disk: an applied change leaves the edited preset dirty until the
user saves it through Orca's normal preset workflow. Values use Orca's serialized config
syntax (numbers plain, percentages with ``%``, booleans ``1``/``0``, lists ``;``-separated).
"""

import asyncio

import orca

CATEGORIES = ("process", "printer", "filament")

PAGE = """<!doctype html>
<html><head><meta charset="utf-8">
<style>
  body { font: 13px/1.4 system-ui, sans-serif; margin: 16px; color: #222; }
  fieldset { border: 1px solid #ccc; border-radius: 6px; margin: 0 0 12px; padding: 10px 12px; }
  legend { font-weight: 600; padding: 0 4px; }
  label { display: inline-block; min-width: 90px; }
  input, select { font: inherit; padding: 3px 5px; margin: 2px 0; }
  input[type=text] { width: 260px; }
  .row { margin: 3px 0; }
  button { font: inherit; padding: 5px 10px; margin: 2px 6px 2px 0; cursor: pointer; }
  #out { white-space: pre-wrap; background: #f6f6f6; border: 1px solid #ddd; border-radius: 6px;
         padding: 10px; min-height: 70px; }
  .err { color: #b00020; }
  .ok { color: #0a7a2f; }
</style></head>
<body>
<fieldset><legend>Target</legend>
  <div class="row">
    <label>Category</label>
    <select id="category">
      <option value="process">process</option>
      <option value="printer">printer</option>
      <option value="filament">filament</option>
    </select>
  </div>
  <div class="row" id="slotRow" style="display:none">
    <label>Slot (1-based)</label><input id="slot" type="text" value="1" style="width:80px">
  </div>
  <div class="row">
    <label>Preset name</label>
    <input id="preset" type="text" placeholder="(blank = current / slot assignment)">
  </div>
  <div class="row" style="color:#666">Edited now: <span id="current">?</span></div>
</fieldset>

<fieldset><legend>Staged settings (serialized values)</legend>
  <div class="row"><input id="k1" type="text" placeholder="key, e.g. layer_height"> =
    <input id="v1" type="text" placeholder="value, e.g. 0.25"></div>
  <div class="row"><input id="k2" type="text" placeholder="key (optional)"> =
    <input id="v2" type="text" placeholder="value"></div>
</fieldset>

<div>
  <button onclick="send('commit')">Commit</button>
  <button onclick="send('duplicate')">Duplicate key</button>
  <button onclick="send('invalid')">Invalid key</button>
  <button onclick="send('refresh')">Refresh</button>
</div>
<p id="out">Ready.</p>

<script>
const $ = (id) => document.getElementById(id);
function pairs() {
  const out = [];
  for (const n of [1, 2]) {
    const k = $('k' + n).value.trim();
    if (k) out.push({ key: k, value: $('v' + n).value });
  }
  return out;
}
function send(command) {
  const category = $('category').value;
  $('slotRow').style.display = category === 'filament' ? '' : 'none';
  const message = {
    command,
    category,
    preset_name: $('preset').value.trim(),
    slot: parseInt($('slot').value, 10) || 1,
    pairs: pairs(),
  };
  orca.postMessage(message);
}
function show(payload) {
  if (payload.error) { $('out').className = 'err'; $('out').textContent = payload.error; return; }
  if (payload.message) { $('out').className = 'ok'; $('out').textContent = payload.message; }
  if (payload.current !== undefined) $('current').textContent = payload.current;
}
orca.onMessage(show);
send('refresh');
</script>
</body></html>"""


async def _begin(category, preset_name, slot):
    """Create a transaction for one category, following the two begin_update arities.

    begin_update() is awaitable: it suspends until any selection / unsaved-edit dialog is
    answered, without blocking Orca's UI thread.
    """
    editor = getattr(orca.host.preset, category)
    # The binding takes a plain string, so an empty field means "" (keep the current / slot target).
    if category == "filament":
        return await editor.begin_update(slot, preset_name)
    return await editor.begin_update(preset_name)


def _result(payload, category):
    return {
        "category": category,
        "target_name": payload.target_name,
        "selection_changed": payload.selection_changed,
        "slot_number": payload.slot_number,
        "slot_assignment_changed": payload.slot_assignment_changed,
        "shared_slots": list(payload.shared_slots),
        "keys": list(payload.keys),
        "dirty": payload.dirty,
    }


def _current(bundle, category, slot):
    if category == "filament":
        presets = list(bundle.current_filament_presets())
        if 1 <= slot <= len(presets) and presets[slot - 1] is not None:
            return "%s (slot %d)" % (presets[slot - 1].name, slot)
        return "<slot %d has no preset>" % slot
    if category == "printer":
        return bundle.current_printer_preset().name
    return bundle.current_process_preset().name


class PresetTransactionDemo(orca.script.ScriptPluginCapabilityBase):
    window = None
    category = "process"
    slot = 1
    loop = None

    def get_name(self):
        return "Preset transaction demo"

    async def execute(self):
        """Open the window and drive its messages, awaiting begin_update per command.

        Runs on the background Script worker's asyncio loop. The window's on_message fires on
        the UI thread, so it only enqueues; execute consumes the queue here, where awaits are
        legal, and keeps the loop alive until the window closes.
        """
        try:
            self.loop = asyncio.get_running_loop()
            self._messages = asyncio.Queue()
            self.category = "process"
            self.slot = 1
            self.window = orca.host.ui.create_window(
                title="Preset transaction demo", html=PAGE, width=760, height=520,
                on_message=self._enqueue, on_close=self._on_close,
            )
            try:
                while True:
                    message = await self._messages.get()
                    if message is None:  # sentinel: the window closed
                        break
                    await self._handle(message)
            finally:
                self.window = None
            return orca.ExecutionResult.success("Preset transaction demo closed.")
        except Exception as error:
            text = "Preset transaction demo failed: %s" % error
            orca.host.ui.message(text, title="Preset transaction demo", buttons="ok", icon="error")
            return orca.ExecutionResult.failure(orca.PluginResult.RecoverableError, text)

    # --- UI thread -> worker loop hand-off ------------------------------------------------
    def _enqueue(self, message):
        """on_message: runs on the UI thread. Hand the message to the worker loop."""
        try:
            self.loop.call_soon_threadsafe(self._messages.put_nowait, message)
        except Exception:
            # The loop is already gone (plugin unloading); drop the message.
            pass

    def _on_close(self):
        """on_close: runs on the UI thread. Wake execute so it returns and the loop closes."""
        try:
            self.loop.call_soon_threadsafe(self._messages.put_nowait, None)
        except Exception:
            pass

    # --- worker loop ----------------------------------------------------------------------
    async def _handle(self, message):
        message = message or {}
        category = message.get("category", "process")
        if category not in CATEGORIES:
            self._post(error="Unknown category %r" % (category,))
            return
        self.category = category
        self.slot = message.get("slot", 1) if isinstance(message.get("slot"), int) else 1
        preset_name = message.get("preset_name") or ""

        try:
            command = message.get("command")
            if command == "refresh":
                self._post()
                return

            if command == "duplicate":
                transaction = await _begin(category, preset_name, self.slot)
                transaction.set("layer_height", "0.2")
                transaction.set("layer_height", "0.25")
                self._post(error="Duplicate key was not rejected (unexpected)")
                return

            if command == "invalid":
                transaction = await _begin(category, preset_name, self.slot)
                transaction.set("this_is_not_a_preset_setting", "1")
                transaction.commit()
                self._post(error="Invalid key was not rejected (unexpected)")
                return

            if command == "commit":
                pairs = message.get("pairs") or []
                if not pairs:
                    self._post(error="Stage at least one key/value row.")
                    return
                transaction = await _begin(category, preset_name, self.slot)
                for entry in pairs:
                    key = entry.get("key")
                    if not isinstance(key, str) or not key:
                        raise RuntimeError("A setting key is required in every row")
                    value = entry.get("value")
                    transaction.set(key, value if isinstance(value, str) else str(value))
                result = transaction.commit()
                self._post(
                    payload=_result(result, category),
                    message="Committed %d setting(s) to %s \"%s\"."
                            % (len(result.keys), category, result.target_name),
                )
                return

            self._post(error="Unknown command %r" % (command,))
        except Exception as error:
            self._post(error="%s: %s" % (type(error).__name__, error))

    def _post(self, payload=None, message="", error=""):
        payload = dict(payload or {})
        if message:
            payload["message"] = message
        if error:
            payload["error"] = error
        try:
            payload["current"] = _current(orca.host.preset_bundle(), self.category, self.slot)
        except Exception as state_error:
            payload["current"] = "unavailable: %s" % state_error
        if self.window is not None and self.window.is_open():
            self.window.post(payload)


@orca.plugin
class PresetTransactionDemoPackage(orca.base):
    def register_capabilities(self):
        orca.register_capability(PresetTransactionDemo)
