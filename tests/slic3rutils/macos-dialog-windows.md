# macOS dialog window regression

The native smoke test needs a logged-in macOS desktop. It deliberately has a separate,
opt-in target: normal headless unit tests must not open windows.
The executable terminates after 30 seconds if desktop initialization or a modal loop hangs.

Build Release dependencies with the macOS wxWidgets patches, configure Orca with tests
(`./build_release_macos.sh -s -a arm64 -T`), then:

```sh
cmake --build build/arm64 --config Release --target macos_dialog_windows_tests
build/arm64/tests/slic3rutils/Release/macos_dialog_windows_tests
```

The smoke test covers native window class, window-menu registration including a hidden
rename/reopen, collection behaviour, nested `ShowModal` inside `wxWindowDisabler`, and
negative cases for frameless/transient dialogs, popup windows and tool frames. Run the
same test against unpatched wxWidgets to reproduce the native-class/list regression.
It does not prove Mission Control, Contexts or Orca's activation callbacks work.

## Application acceptance

Use a disposable Orca configuration/project; do not overwrite the user's presets. Record
the Orca commit, wx patch state, macOS and Contexts versions with the results.

| Scenario | Expected result |
| --- | --- |
| Open filament settings, printer settings, Preferences and a non-resizable confirmation | Each ordinary dialog is selectable in App Exposé/Mission Control, the Dock window list and Contexts. |
| Switch to another app; return using Cmd+Tab, Dock and Contexts | The active blocking dialog is brought forward; the main window stays blocked. |
| Repeat across Spaces and with a fullscreen main window | The dialog remains reachable; no duplicate/all-Spaces or always-on-top window is introduced. |
| Open a nested confirmation or native file dialog; switch away/back | The innermost modal/sheet remains above its parent. Closing it returns control to the prior dialog. |
| Reopen already shown settings | Existing window comes forward without moving or creating another modal session. |
| Close using Escape and the title-bar close button; repeat | The window disappears from lists, and the main window becomes usable. |
| Close/destroy a dialog before a queued activation callback runs | No crash or resurrection; the callback resolves the current native modal instead. |
| Switch between modeless windows | No unrelated window steals focus. |
| Open a dropdown, tooltip, frameless popup or tool window | Existing behaviour is retained; it is not added to ordinary dialog listings. |

While a modal is open, window switching must not bypass its input blocking. A successful
build or smoke test is not a substitute for the application/Contexts checks above.
