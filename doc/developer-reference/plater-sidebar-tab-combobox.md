### !! incomplete, possibly inaccurate, being updated with new info !!

## [`Plater`](../../src/slic3r/GUI/Plater.hpp)

Refers to the entire application. The whole view, file loading, project saving and loading is all managed by this class. This class contains members for the model viewer, the sidebar, gcode viewer and everything else.

## [`Sidebar`](../../src/slic3r/GUI/Plater.hpp)

This is relating the the sidebar in the application window

<img src="../images/full-sidebar.png" alt="Example Image" width="320">

## [`ComboBox`](../../src/slic3r/GUI/Widgets/ComboBox.hpp)

The drop down menus where you can see and select presets

<img src="../images/combobox.png" alt="Example Image" width="320">

## [`Tab`](../../src/slic3r/GUI/Tab.hpp)

Refers to the various windows with settings. e.g. the Popup to edit printer or filament preset. Also the section to edit process preset and the object list. These 4 are managed by `TabPrinter`, `TabFilament`, `TabPrint` and `TabPrintModel` respectively.

<img src="../images/tab-popup.png" alt="Example Image" width="320">
