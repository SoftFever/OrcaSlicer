# Layer Height

Layer height determines the vertical thickness of each printed layer, significantly impacting both print quality and printing time.

Smaller layer heights produce better quality (smoother surfaces, less visible layer lines, better curved details, [improved overhang angles](quality_settings_overhangs)) but increase print time and can cause flow inconsistencies at high speeds.

- [Quick Reference](#quick-reference)
- [Layer Height Guidelines](#layer-height-guidelines)
- [First Layer Height](#first-layer-height)
- [Stepper Motor Magic Numbers](#stepper-motor-magic-numbers)

## Quick Reference

| Nozzle Size | Min    | Max    | [First Layer Height](#first-layer-height) |
|-------------|--------|--------|-------------------------------------------|
| 0.2mm       | 0.04mm | 0.16mm | 0.12mm                                    |
| 0.3mm       | 0.06mm | 0.24mm | 0.18mm                                    |
| 0.4mm       | 0.08mm | 0.32mm | 0.25mm                                    |
| 0.6mm       | 0.12mm | 0.48mm | 0.35mm                                    |
| 0.8mm       | 0.16mm | 0.64mm | 0.45mm                                    |
| 1.0mm       | 0.20mm | 0.80mm | 0.55mm                                    |

## Layer Height Guidelines

Usually, the optimal range for layer height is between 20% and 80% of the nozzle diameter.

- **Below 20%:** Flow inconsistencies and "fish scale" patterns may occur, especially at high speeds.
- **Over 80%:** Increased risk of layer adhesion issues and reduced print quality.

## First Layer Height

Controls the thickness of the initial layer.  
A thicker first layer improves bed adhesion and compensates for build surface imperfections.

**Recommended:** 0.25mm for 0.4mm nozzle (62.5% of nozzle diameter)  
**Maximum:** 65% of nozzle diameter

## Stepper Motor Magic Numbers

For optimal print quality, consider using layer heights that align with your printer's Z-axis stepper motor resolution. These "magic numbers" ensure that each layer height corresponds to complete stepper motor steps, reducing micro-stepping inaccuracies.

> [!IMPORTANT]
> **Modern printers** may not benefit from magic numbers thanks to high-resolution drivers.

**Common magic numbers for 0.9° stepper motors (400 steps/mm):**

- 0.1mm, 0.15mm, 0.2mm, 0.25mm, 0.3mm

**Common magic numbers for 1.8° stepper motors (200 steps/mm):**

- 0.1mm, 0.2mm, 0.3mm, 0.4mm

**To calculate your magic numbers:**

1. Determine your Z-axis steps/mm (check firmware or calculate: steps_per_revolution ÷ lead_screw_pitch)
2. Use layer heights that result in whole numbers when multiplied by steps/mm
3. Formula: Magic_Layer_Height = whole_number ÷ steps_per_mm
