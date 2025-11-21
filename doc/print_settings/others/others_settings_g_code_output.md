# G-Code Output

These settings control how G-code is generated and formatted. They impact readability, file size, print behavior, and compatibility with firmware and post-processing tools.

- [Reduce Infill Retraction](#reduce-infill-retraction)
- [Add line number](#add-line-number)
- [Verbose G-code](#verbose-g-code)
- [Label Objects](#label-objects)
- [Exclude Objects](#exclude-objects)
- [Filename Format](#filename-format)

## Reduce Infill Retraction

When enabled, the slicer will skip retractions for travel moves that occur entirely inside infill regions. This reduces the number of retractions and can speed up printing for complex models, but it may increase oozing or stringing inside infill. Slicing time may also increase slightly.

**Recommended** when internal cosmetic quality is not critical and you want fewer retractions.

## Add line number

Prefix each G-code line with a sequential line number (N1, N2, ...). Useful for debugging or tools that expect numbered G-code.

## Verbose G-code

Include descriptive comments for G-code lines and blocks to make the file human-readable and easier to debug.  
Verbose mode produces much larger files and may slow down SD-card printing on some printers.

## Label Objects

Insert comments that label moves with the object they belong to (object index or name). This is useful for integrations such as OctoPrint's Cancel Object plugin and for human inspection of the G-code.

> [!IMPORTANT]
> Object labelling is not compatible with Single-Extruder Multi-Material configurations or with "Wipe into Object" / "Wipe into Infill" strategies.  
> When those features are active, labels may be omitted.

## Exclude Objects

Add an `EXCLUDE OBJECT` marker or command in the exported G-code for objects flagged as excluded. This helps post-processors or custom scripts recognise excluded parts.

## Filename Format

Define a filename template for exported G-code. Templates may include tokens like project name, date, or other metadata to produce consistent and informative filenames on export.

For example:

```c++
{input_filename_base}_{filament_type[initial_tool]}_{print_time}.gcode
```

Can be used to generate filenames like `OrcaCube_PLA_1h15m.gcode`.

> [!TIP]
> Check [Built in placeholders variables](built-in-placeholders-variables) for available tokens and their meanings.
