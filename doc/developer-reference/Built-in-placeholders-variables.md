# Placeholders Variables

This section describes the general built-in placeholders variables available for use in G-code scripts and configurations.

- [Global Slicing State](#global-slicing-state)
  - [Read Only](#read-only)
  - [Read Write](#read-write)
- [Slicing State](#slicing-state)
- [Print Statistics](#print-statistics)
- [Objects Info](#objects-info)
- [Dimensions](#dimensions)
- [Temperatures](#temperatures)
- [Timestamps](#timestamps)
- [Presets](#presets)

## Global Slicing State

### Read Only

- **zhop**: Contains Z-hop present at the beginning of the custom G-code block.

### Read Write

- **e_position[]**: Current position of the extruder axis. Only used with absolute extruder addressing.
- **e_restart_extra[]**: Currently planned extra extruder priming after de-retraction.
- **e_retracted[]**: Retraction state at the beginning of the custom G-code block. If the custom G-code moves the extruder axis, it should write to this variable so OrcaSlicer knows where it travels from when it gets control back.
- **position[]**: Current position of the extruder axis. Only used with absolute extruder addressing.

## Slicing State

- **current_extruder**: Zero-based index of currently used extruder.
- **current_object_idx**: Zero-based index of currently printed object.
- **has_wipe_tower**: Whether or not wipe tower is being generated in the print.
- **initial_extruder**: Zero-based index of the first extruder used in the print. Same as initial_tool.
- **initial_tool**: Zero-based index of the first extruder used in the print. Same as initial_extruder.
- **is_extruder_used**: Vector of booleans stating whether a given extruder is used in the print.
- **has_single_extruder_multi_material_priming**: Whether or not single extruder multi-material priming is used in the print.
- **initial_no_support_extruder**: Zero-based index of the first extruder used for printing without support. Same as initial_no_support_tool.
- **in_head_wrap_detect_zone**: Indicates if the first layer overlaps with the head wrap zone.

## Print Statistics

- **extruded_volume**: Total filament volume extruded per extruder during the entire print.
- **extruded_volume_total**: Total volume of filament used during the entire print.
- **extruded_weight**: Total filament weight extruded per extruder during the entire print.
- **extruded_weight_total**: Total weight of filament used during the entire print.
- **total_print_time**: Total time taken for the print.
- **total_layer_count**: Total number of layers in the print.

## Objects Info

- **num_objects**: Total number of objects in the print.
- **num_instances**: Total number of object instances in the print, summed over all objects.
- **scale[]**: Contains a string with the information about what scaling was applied to the individual objects. Indexing of the objects is zero-based (first object has index 0).
- **input_filename_base**: Source filename of the first object, without extension.
- **input_filename**: Full input filename of the first object.
- **plate_name**: Name of the plate sliced.

## Dimensions

- **first_layer_print_convex_hull**: Vector of points of the first layer convex hull. Each element has the following format: '[x, y]' (x and y are floating-point numbers in mm).
- **first_layer_print_min**: Bottom-left corner of first layer bounding box.
- **first_layer_print_max**: Top-right corner of first layer bounding box.
- **first_layer_print_size**: Size of the first layer bounding box.
- **print_bed_min**: Bottom-left corner of print bed bounding box.
- **print_bed_max**: Top-right corner of print bed bounding box.
- **print_bed_size**: Size of the print bed bounding box.
- **first_layer_center_no_wipe_tower**: First layer center without wipe tower.
- **first_layer_height**: Height of the first layer.

## Temperatures

- **bed_temperature**: Vector of bed temperatures for each extruder/filament.
- **bed_temperature_initial_layer**: Vector of initial layer bed temperatures for each extruder/filament. Provides the same value as first_layer_bed_temperature.
- **bed_temperature_initial_layer_single**: Initial layer bed temperature for the initial extruder. Same as bed_temperature_initial_layer[initial_extruder].
- **chamber_temperature**: Vector of chamber temperatures for each extruder/filament.
- **overall_chamber_temperature**: Overall chamber temperature. This value is the maximum chamber temperature of any extruder/filament used.
- **first_layer_bed_temperature**: Vector of first layer bed temperatures for each extruder/filament. Provides the same value as bed_temperature_initial_layer.
- **first_layer_temperature**: Vector of first layer temperatures for each extruder/filament.

## Timestamps

- **timestamp**: String containing current time in yyyyMMdd-hhmmss format.
- **year**: Current year.
- **month**: Current month.
- **day**: Current day.
- **hour**: Current hour.
- **minute**: Current minute.
- **second**: Current second.

## Presets

Each preset's ([Print process settings](home#process-settings), [Filament/Material settings](home#material-settings), [Printer settings](home#printer-settings)) variables can be used in your G-code scripts and configurations.  

> [!TIP]
> To know the variable name you can hover your mouse over the variable in the UI.
