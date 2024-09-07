# Layer Height

This setting controls how tall each printed layer will be. Typically, a smaller layer height produces a better-looking part with less jagged edges, especially around curved sections (like the top of a sphere). However, lower layer heights mean more layers to print, proportionally increasing print time.

### Tips:
1. **The optimal layer height depends on the size of your nozzle**. The set layer height must not be taller than 80% of the diameter of the nozzle, else there is little "squish" between the printed layer and the layer below, leading to weaker parts.

2. While technically there is no limit to how small a layer height one can use, **typically most printers struggle to print reliably with a layer height that is smaller than 20% of the nozzle diameter**. This is because with smaller layer heights, less material is extruded per mm and, at some point, the tolerances of the extruder system result in variations in the flow to such an extent that visible artifacts occur, especially if printing at high speeds. 

For example, it is not uncommon to see "fish scale" type patterns on external walls when printing with a 0.4 mm nozzle at 0.08 mm layer height at speeds of 200mm/sec+. If you observe that pattern, simply increase your layer height to 30% of your nozzle height and/or slow down the print speed considerably.

# First Layer Height

This setting controls how tall the first layer of the print will be. Typically, this is set to 50% of the nozzle width for optimal bed adhesion.

### Tip:
A thicker first layer is more forgiving to slight variations to the evenness of the build surface, resulting in a more uniform, visually, first layer. Set it to 0.25mm for a 0.4mm nozzle, for example, if your build surface is uneven or your printer has a slightly inconsistent z offset between print runs. However, as a rule of thumb, try not to exceed 65% of the nozzle width so as to not compromise bed adhesion too much.
