# Pellet Flow Coefficient

Large format printers with print volumes in the order of 1m^3 generally use pellets for printing.
The overall tech is very similar to FDM printing.
It is FDM printing, but instead of filaments, it uses pellets.

The difference here is that where filaments have a filament_diameter that is used to calculate
the volume of filament ingested, pellets have a particular flow_coefficient that is empirically
devised for that particular pellet.

pellet_flow_coefficient is basically a measure of the packing density of a particular pellet.
Shape, material and density of an individual pellet will determine the packing density and
the only thing that matters for 3d printing is how much of that pellet material is extruded by
one turn of whatever feeding mehcanism/gear your printer uses. You can emperically derive that
for your own pellets for a particular printer model.

We are translating the pellet_flow_coefficient into filament_diameter so that everything works just like it
does already with very minor adjustments.

```math
\text{filament\_diameter} = \sqrt{\frac{4 \times \text{pellet\_flow\_coefficient}}{\pi}}
```

sqrt just makes the relationship between flow_coefficient and volume linear.

higher packing density -> more material extruded by single turn -> higher pellet_flow_coefficient -> treated as if a filament of larger diameter is being used
All other calculations remain the same for slicing.
