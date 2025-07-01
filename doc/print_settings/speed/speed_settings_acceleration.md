# Acceleration

Acceleration in 3D printing is usually set on the printer's firmware settings.  
This setting will try to override the acceleration when [normal printing acceleration](#normal-printing) value is different than 0.
Orca will limit the acceleration to not exceed the acceleration set in the Printer's Motion Ability settings.

## Normal printing

The default acceleration of both normal printing and travel.

## Outer wall

Acceleration for outer wall printing. This is usually set to a lower value than normal printing to ensure better quality.

## Inner wall

Acceleration for inner wall printing. This is usually set to a higher value than outer wall printing to improve speed.

## Bridge

Acceleration of bridges. If the value is expressed as a percentage (e.g. 50%), it will be calculated based on the outer wall acceleration.

## Sparse infill

Acceleration of sparse infill. If the value is expressed as a percentage (e.g. 100%), it will be calculated based on the default acceleration.

## Internal solid infill

Acceleration of internal solid infill. If the value is expressed as a percentage (e.g. 100%), it will be calculated based on the default acceleration.

## Initial layer

Acceleration of initial layer. Using a lower value can improve build plate adhesion.

## Top surface

Acceleration of top surface infill. Using a lower value may improve top surface quality.

## Travel

Acceleration of travel moves. This is usually set to a higher value than normal printing to reduce travel time.
