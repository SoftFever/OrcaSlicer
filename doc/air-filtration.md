OrcaSlicer use `M106 P3` command to control air-filtration/exhaust fan.

If you are using Klipper, you can define a `M106` macro to control the both normal part cooling fan and auxiliary fan and exhaust fan.  
Below is a reference configuration for Klipper.   
*Note: Don't forget to change the pin name to the actual pin name you are using in the configuration*

```
# instead of using [fan], we define the default part cooling fan with [fan_generic] here
# this is the default part cooling fan
[fan_generic fan0]
pin: PA7
cycle_time: 0.01
hardware_pwm: false

# this is the auxiliary fan
# comment out it if you don't have auxiliary fan
[fan_generic fan2]
pin: PA8
cycle_time: 0.01
hardware_pwm: false

# this is the exhaust fan
# comment out it if you don't have exhaust fan
[fan_generic fan3]
pin: PA9
cycle_time: 0.01
hardware_pwm: false

[gcode_macro M106]
gcode:
    {% set fan = 'fan' + (params.P|int if params.P is defined else 0)|string %}
    {% set speed = (params.S|float / 255 if params.S is defined else 1.0) %}
    SET_FAN_SPEED FAN={fan} SPEED={speed}

```
