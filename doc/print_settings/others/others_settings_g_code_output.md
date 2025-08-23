# G-Code Output

G-code output settings control how the G-code is generated and formatted. These settings can affect the readability of the G-code, the efficiency of the print, and compatibility with various firmware, printers and post-processing tools.

## Reduce Infill Retraction

Don't retract when the travel is in infill area absolutely. That means the oozing can't been seen. This can reduce times of retraction for complex model and save printing time, but make slicing and G-code generating slower.

## Add line number

Enable this to add line number(Nx) at the beginning of each G-code line.

## Verbose G-code

Enable this to get a commented G-code file, with each line explained by a descriptive text. If you print from SD card, the additional weight of the file could make your firmware slow down.

## Label Objects

Enable this to add comments into the G-code labeling print moves with what object they belong to, which is useful for the Octoprint CancelObject plugin. This settings is NOT compatible with Single Extruder Multi Material setup and Wipe into Object / Wipe into Infill.

## Exclude Objects

Enable this option to add EXCLUDE OBJECT command in G-code.

## Filename Format

Users can define the project file name when exporting.
