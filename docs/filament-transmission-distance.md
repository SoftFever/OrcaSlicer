# Transmission distance

How deep light gets into a filament, in millimetres: the thickness at which it stops showing what is
printed underneath it. About 1 for an opaque filament, 4 to 7 for a usefully translucent one, 8 and
above for a nearly clear one. It is the number every translucent-layer colour tool is built on.

Nothing in slicing reads it. The option exists so that the value has somewhere to live.

## Why no values ship with the profiles

Every published transmission distance is **per colour**. OrcaSlicer's filament presets are **per
product line**: `Panchroma PLA Matte @base` covers every colour Polymaker sells under that name, and
their own published values for that one line run from about 0.1 to about 4.1. There is no colour to
attach a number to, so putting one on the preset would mean choosing a representative colour - an
editorial guess wearing the clothes of a measurement, and wrong for most spools of that line.

No source states the conditions either: not a layer height, not an instrument, not an illuminant. A
transmission distance without its method is approximate however good its provenance.

So the field ships empty-handed, and the value comes from whoever has the spool.

## Getting a value

- **Measure it.** Calibration -> Transmission distance prints a staircase of the filament over one it
  has to hide; the step where the one underneath stops showing through is the measurement. No second
  tool, no instrument.
- **A meter.** The BIQU AJAX TD-1S and the open-hardware Td-Free measure it directly.
- **A spool tag.** OpenPrintTag carries it as main field 27, `transmission_distance`, so a tagged
  spool can hand over its own number.

## Where published values exist

Recorded here so the question does not have to be asked again, and so that anyone filling in a
profile knows what is safe to copy.

| Source | Licence | Shape |
| --- | --- | --- |
| [OpenPrintTag material database](https://github.com/OpenPrintTag/openprinttag-database) | MIT, Copyright 2025 Prusa Research a.s. | ~125 per-colour values across Prusament, Polymaker and Printed Solid. The only openly licensed set whose numbers trace back to the brand that published them. |
| [Prusa's HueForge TD and hex table](https://help.prusa3d.com/article/hueforge-filament-transparency-values-and-hexcodes_762314) | none stated | ~78 per-colour values for Prusament, measured by Prusa for its own filament. |
| [Polymaker's hex codes and transmission distances](https://wiki.polymaker.com/polymaker-products/more-about-our-products/hex-codes-and-transmission-distances) | none stated | ~400 per-colour values, the largest manufacturer table. |

A manufacturer publishing a measurement of its own product is the clearest case there is, but neither
of the last two grants anything in writing - ask before copying a table wholesale.

**Do not take values from HueForge's bundled database**, and do not take them from an aggregator that
cannot say where its numbers came from: an open licence on a collection does not clean the provenance
of what was collected into it. Some of the large openly licensed datasets say outright that they do
not record where each value came from, and ship importers for HueForge's own library.
