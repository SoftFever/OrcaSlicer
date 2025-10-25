# Ooze prevention

This option will drop the temperature of the inactive extruders to prevent oozing.

## Temperature variation

Temperature difference to be applied when an extruder is not active. The value is not used when 'idle_temperature' in filament settings is set to non-zero value.

## Preheat time

To reduce the waiting time after tool change, Orca can preheat the next tool while the current tool is still in use. This setting specifies the time in seconds to preheat the next tool. Orca will insert a M104 command to preheat the tool in advance.

## Preheat steps

Insert multiple preheat commands (e.g. M104.1). Only useful for Prusa XL. For other printers, please set it to 1.
