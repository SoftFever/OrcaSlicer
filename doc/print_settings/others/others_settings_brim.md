# Brim

Brim is a flat layer printed around the base of a model to help with adhesion to the print bed. It can be useful for models with small footprints or those that are prone to warping.

## Type

This controls the generation of the brim at outer and/or inner side of models.  
Auto means the brim width is analyzed and calculated automatically.

### Painted

Painted will generate a brim only on painted areas of the model in the Prepare tab.

### Outer

Outer will generate a brim around the outer perimeter of the model.
Easier to remove than inner brim but can affect the model's appearance if the brim is not removed cleanly.

### Inner

Inner will generate a brim around the inner perimeter of the model.
More difficult to remove than outer brim and may close the model's inner details, but can hide where the brim was removed.

### Mouse Ears

Mouse ears are small extensions added to the brim to help with adhesion and prevent warping.
Usually this ears are added in the corners of objects to provide additional support and affect the model's appearance less than a full brim.

#### Ear max angle

Maximum angle to let a brim ear appear.  
If set to 0, no brim will be created.  
If set to ~180, brim will be created on everything but straight sections.

#### Ear detection radius

The geometry will be decimated before detecting sharp angles. This parameter indicates the minimum length of the deviation for the decimation.  
0 to deactivate.

## Width

Distance from model to the outermost brim line.

## Brim-Object Gap

A gap between innermost brim line and object can make brim be removed more easily.
