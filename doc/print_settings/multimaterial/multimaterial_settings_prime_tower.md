# Prime Tower

The wiping tower can be used to clean up the residue on the nozzle and "
"stabilize the chamber pressure inside the nozzle, in order to avoid "
"appearance defects when printing objects.

## Width

Width of the prime tower.

## Brim width

Width of the brim around the prime tower.

## Wipe Tower Rotation Angle

Wipe tower rotation angle with respect to x-axis.

## Maximal bridging distance

Maximal distance between supports on sparse infill sections.

## Wipe tower purge lines spacing

Spacing of purge lines on the wipe tower.

## Extra flow for purge

Extra flow used for the purging lines on the wipe tower. This makes the purging lines thicker or narrower than they normally would be. The spacing is adjusted automatically.

## Maximum wipe tower print speed

The maximum print speed when purging in the wipe tower and printing the wipe tower sparse layers. When purging, if the sparse infill speed or calculated speed from the filament max volumetric speed is lower, the lowest will be used instead.  
When printing the sparse layers, if the internal perimeter speed or calculated speed from the filament max volumetric speed is lower, the lowest will be used instead.  
Increasing this speed may affect the tower's stability as well as increase the force with which the nozzle collides with any blobs that may have formed on the wipe tower.  
Before increasing this parameter beyond the default of 90 mm/s, make sure your printer can reliably bridge at the increased speeds and that ooze when tool changing is well controlled.  
For the wipe tower external perimeters the internal perimeter speed is used regardless of this setting.

## Wall type

Wipe tower outer wall type.

### Rectangle

The default wall type, a rectangle with fixed width and height.

### Cone

A cone with a fillet at the bottom to help stabilize the wipe tower.

#### Stabilization cone apex angle

Angle at the apex of the cone that is used to stabilize the wipe tower. Large angle means wider base.

### Rib

Adds four ribs to the tower wall for enhanced stability.

#### Extra rib length

Positive values can increase the size of the rib wall, while negative values can reduce the size. However, the size of the rib wall can not be smaller than that determined by the cleaning volume.

#### Rib width

Width of the rib wall.

#### Fillet wall

The wall of prime tower will fillet.

## No sparse layers

If enabled, the wipe tower will not be printed on layers with no tool changes. On layers with a tool change, extruder will travel downward to print the wipe tower. User is responsible for ensuring there is no collision with the print.
