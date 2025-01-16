### !! incomplete, possibly inaccurate, being updated with new info !!

## [`Plater`](https://github.com/SoftFever/src/slic3r/GUI/Plater.hpp)

Refers to the entire application. The whole view, file loading, project saving and loading is all managed by this class. This class contains members for the model viewer, the sidebar, gcode viewer and everything else.

## [`Sidebar`](https://github.com/SoftFever/src/slic3r/GUI/Plater.hpp)

This is relating the the sidebar in the application window

<img src="/assets/images/full-sidebar.png" alt="Example Image" width="320">

## [`ComboBox`](https://github.com/SoftFever/src/slic3r/GUI/Widgets/ComboBox.hpp)

The drop down menus where you can see and select presets

<img src="/assets/images/combobox.png" alt="Example Image" width="320">

## [`Tab`](https://github.com/SoftFever/src/slic3r/GUI/Tab.hpp)

Refers to the various windows with settings. e.g. the Popup to edit printer or filament preset. Also the section to edit process preset and the object list. These 4 are managed by `TabPrinter`, `TabFilament`, `TabPrint` and `TabPrintModel` respectively.

<img src="/assets/images/tab-popup.png" alt="Example Image" width="320">
