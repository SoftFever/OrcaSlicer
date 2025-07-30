# Template metalanguage

## Basic instructions

`[±]α[*ℤ or !][solid or joint sign, оr its combinations][-][ℕ, B or T][length modifier][* or !]`  - full length template instruction for the **sparse** infill<br/>

`[±]α[*ℤ or !][joint sign][-][ℕ][* or !]` - full length template instruction for the **solid** infill<br/>

`[±]α*` - just setting an initial rotation angle<br/>

`[solid or joint sign]ℕ[!]` - putting the solid layers ℕ times without them rotating<br/>

`[B or T][!]` - putting the solid layers according to the number of bottom or top shell layers without them rotating<br/>

> `[...]` - values in square brackets are optional

### Defined angle
`[±]α` - command of setting of rotation infill angle (for joint infills at some height range, this angle is finite):<br/>
   - `α` - just set absolute angle 0...360<br/>
   - `+α` - set positive relative angle CCW<br/>
   - `-α` - set negative relative angle CW<br/>

### Runtime instructions
`[*, *ℤ or !]` - runtime instructions:<br/>
   - `*` - the mark of "dumb" instruction. Its need for setting an initial angle. No further action will be taken<br/>
   - `*ℤ` - repeat the instruction ℤ times<br/>
   - `!` - the one-time running instruction<br/>

### Solid sign
`[solid sign]` - the mark for insert solid layer:<br/>
   - `D` - insert native sparse patterned layer but with 100% density<br/>
   - `S` - insert user-defined solid layer<br/>
   - `O` - insert Concentric solid layer<br/>
   - `M` - insert Monotonic solid layer<br/>
   - `R` - insert Rectilinear solid layer<br/>

### Joint sign
`[joint sign]` - the symbol which determinate method of connection for turning of the infill:<br/>

   - `/` - linear displacement of the infill<br/>
   <img height="200" align="bottom" alt="linear joint" src="../../images/Template-metalanguage/lin-joint.png" /> `+22.5/50%`

   - `#` - infill of multiple layers with vertical displacement at finish angle<br/>
   <img height="200" align="bottom" alt="multiple joint" src="../../images/Template-metalanguage/%23-joint.png" /> `+22.5#50%`

   - `#-` - infill of multiple layers with vertical displacement at initial angle<br/>
   <img height="200" align="bottom" alt="multiple joint @ initial angle" src="../../images/Template-metalanguage/%23--joint.png" /> `+22.5#-50%`

   - `|` - infill of multiple layers with vertical displacement at middle angle<br/>
   <img height="200" align="bottom" alt="multiple joint @ middle angle" src="../../images/Template-metalanguage/div-joint.png" /> `+22.5|50%`

   - `N` - infill form by sinus function (vertical connection)<br/>
   <img height="200" align="bottom" alt="v-sinus joint" src="../../images/Template-metalanguage/N-joint.png" /> `+22.5N50%`

   - `n` - infill form by sinus function (vertical connection, lazy)<br/>
   <img height="200" align="bottom" alt="v-sinus joint, lazy" src="../../images/Template-metalanguage/n_-joint.png" /> `+22.5n50%`

   - `Z` - infill form by sinus function (horizontal connection)<br/>
   <img height="200" align="bottom" alt="h-sinus joint" src="../../images/Template-metalanguage/Z-joint.png" /> `+22.5Z50%`

   - `z` - infill form by sinus function (horizontal connection, lazy)<br/>
   <img height="200" align="bottom" alt="h-sinus joint, lazy" src="../../images/Template-metalanguage/z_-joint.png" /> `+22.5z50%`

   - `L` - infill form by quarter of circle  (horizontal to vertical connection)<br/>
   <img height="200" align="bottom" alt="vh-quarter joint" src="../../images/Template-metalanguage/L-joint.png" /> `+22.5L50%`

   - `l` -  infill form by quarter of circle (vertical to horizontal connection)<br/>
   <img height="200" align="bottom" alt="hv-quarter joint" src="../../images/Template-metalanguage/l_-joint.png" /> `+22.5l50%`

   - `U` - infill form by squared function<br/>
   <img height="200" align="bottom" alt="squared joint" src="../../images/Template-metalanguage/U-joint.png" /> `+22.5U50%`

   - `u-` - infill form by squared function (inverse)<br/>
   <img height="200" align="bottom" alt="squared joint, inverse" src="../../images/Template-metalanguage/u_-joint.png" /> `+22.5u-50%`

   - `Q` - infill form by cubic function<br/>
   <img height="200" align="bottom" alt="cubic joint" src="../../images/Template-metalanguage/Q-joint.png" /> `+22.5Q50%`

   - `q-` - infill form by cubic function (inverse)<br/>
   <img height="200" align="bottom" alt="cubic joint, inverse" src="../../images/Template-metalanguage/q_-joint.png" /> `+22.5q-50%`

   - `$` - infill form by arcsinus method<br/>
   <img height="200" align="bottom" alt="arcsinus joint" src="../../images/Template-metalanguage/%24-joint.png" /> `+22.5$50%`

   - `~` - infill form random angle<br/>
   <img height="200" align="bottom" alt="random joint" src="../../images/Template-metalanguage/%7E-joint.png" /> `+22.5~50%`

   - `^` - infill form pseudorandom angle<br/>
   <img height="200" align="bottom" alt="pseudorandom joint" src="../../images/Template-metalanguage/%5E-joint.png" /> `+22.5^50%`

### Counting 
`[-]ℕ` - counting the distance at which the turn will take place:<br/>
   - `ℕ` - the count will take place by ℕ layers<br/>
   <img width="160" height="160" align="bottom" alt="image" src="https://github.com/user-attachments/assets/d97220a4-9ae4-42e1-9e77-63c6cd85948a" /> `+22.5/50%`

   - `-ℕ` - indicates that the joint form will be flipped upward<br/>
   <img width="160" height="160" align="bottom" alt="image" src="https://github.com/user-attachments/assets/9bdfb759-a757-464e-aaf1-aa5e9b6265ae" /> `+22.5/-50%`

   - `B` - the count will take place by next layers equals of bottom_shell_layers parameter<br/>
   - `T` - the count will take place by next layers equals of top_shell_layers parameter<br/>

### Length modifier
`ℕ[length modifier]` - the distance at which the specified turn will take place:<br/>
   - `ℕmm` - the distance in millimeters<br/>
   - `ℕcm` - the distance in centimeters<br/>
   - `ℕm` - the distance in meters<br/>
   - `ℕ'` - the distance in feet<br/>
   - `ℕ"` - the distance in inches<br/>
   - `ℕ#` - the distance in range of standard height of ℕ layers<br/>
   - `ℕ%` - the distance of model height in percents<br/>

## Description of instructions and examples
Each instruction is written by combination of symbols and numbers and separated by a comma or a space.
For more complex instructions, an autoformat is used to make the template easier to read.
All examples are shown with a 5% density rectilinear infill on model of cube 20x20x20mm which has 100 layers of 0.2mm thickness. Without walls and upper and lower shells. Initial angle is 0. 

### Simple absolute instructions
They include a simple definition of the angle for each layer. Note that the initial setting of this angle is also affected by the value in the sparse or solid infill angle field.

`0`, `15`, `45.5`, `256.5605`... - just fill at the existing angle. The initial direction starts at the X-axis one, and the acceptable range of values is from 0 to 360<br/>

<img width="160" height="160" align="bottom" alt="image" src="https://github.com/user-attachments/assets/eba8282e-b2ea-4b04-93a7-70db3018ffaf" /> `0` as also `+0`, `-0` or just empty template<br/>

<img width="160" height="160" align="bottom" alt="image" src="https://github.com/user-attachments/assets/f8222867-b43f-4935-bf29-7216dafe13a1" /> `45`<br/>

`0%`, `10%`, `25%`, `100%`... - infill angle determine from relative terms from a full turn of 360 degree rotation. Rotate by 0, 36, 90, and 0 degrees.<br/>

<img width="160" height="160" align="bottom" src="https://github.com/user-attachments/assets/a9761199-e2f5-485f-b5b4-0009f7b07fc1" /> `25%` - the equivalent of `90` instruction.<br/>

<img width="160" height="160" align="bottom" alt="image" src="https://github.com/user-attachments/assets/e6f48755-e24b-4098-b8e2-de3db655e67c" /> `0, 30` - is a simple alternation through each layer in the direction of 0 and 30 degrees.<br/>

`30, 60, 90, 120, 150, 0` - a more complex command defines a turn every layer at 30 degrees. At the end of the template line, the next instruction is read first, and this process continues until the entire height of the model is filled in.

### Relative instructions
`+30` - this is a short instruction by counterclockwise rotation. The equivalent of `30, 60, 90, 120, 150, 180, 210, 240, 270, 300, 330, 0` or `30, 60, 90, 120, 150, 0` instruction.<br/>
<img width="160" height="160" align="bottom" src="https://github.com/user-attachments/assets/26122668-dc33-4a31-b0c5-70433da2dce5" /><br/>

`-30` - this is the same instruction, but with clockwise rotation. The equivalent of `330, 300, 270, 240, 210, 180, 150, 120, 90, 60, 30, 0` or `330, 300, 270, 240, 210, 0` instruction.<br/>

`+150` - you can specify a different multiple of the irrational angle for better fill dispersion = `150, 300, 90, 240, 30, 180, 330, 120, 270, 60, 210, 0` ...<br/>

`+45` The equivalent of `45, 90, 135, 180, 225, 270, 315, 0` or `45, 90, 135, 0` instruction.<br/>
<img width="160" height="160" align="bottom" alt="image" src="https://github.com/user-attachments/assets/901a2b89-a064-4d6c-92c2-e14b34e0ab7a" /><br/>

`+90` The equivalent of `90, 180, 270, 0` or `90, 0` instruction.<br/>
<img width="160" height="160" align="bottom" alt="image" src="https://github.com/user-attachments/assets/e878456d-86b7-4eb3-8a98-c75214b2eb16" /><br/>

`+15%` -  useful for dividing angles on a decimal basis = `54, 108, 162, 270, 324, 18, 72, 126, 180, 234, 288, 342, 36, 90, 144, 196, 252, 306, 0` ...<br/>

`+30, +90` - a complex instruction setting the rotation of each layer in these positions = `30, 120, 150, 240, 270, 0` ...<br/>
<img width="160" height="160" align="bottom" alt="image" src="https://github.com/user-attachments/assets/63c04770-e4ce-4a26-86de-497233fc35a6" /><br/>

`0, +30, +90` - a complex instruction setting the rotation of each layer in these positions = `0, 30, 120` ...<br/>
<img width="160" height="160" align="bottom" alt="image" src="https://github.com/user-attachments/assets/8d213f62-54df-4c6e-961c-dcb4c7b9077c" /><br/>

### Repetitive, adjusting and one-time instructions
`5, 10, +20` - simple instructions without modifiers. Sets the angles in sequence = 5, 10, 30, 5, 10, 30, 5, 10 ...<br/>

`5, 10*, +20` - the `*` sign sets the initial angle without quantitative designation without the layer processing. It is useful for setting the orientation of a group of layers, which will be described below. Sets the angles in sequence = 5, 30, 5, 30, 5, 30, 5 ...<br/>

`5, 10!, +20` - the `!` sign indicates that instruction will be executed only once for the entire height of the model. Sets the angles in sequence = 5, 10, 30, 5, 25, 5, 25, 5  ...<br/>

`5, 10*!, +20` - the combination `*` and `!` signs also usable = 5, 30, 5, 25, 5, 25, 5, 25 ...<br/>

`5, 10*3, +20` - if a number is written after the `*` sign, it indicates the number of repetitions of this instruction. Sets the angles in sequence = 5, 10, 10, 10, 30, 5, 10, 10, 10, 30, 5, 10 ...<br/>

### Range instructions
A combined set of layers will be organized, where the rotation of one layer relative to the other will also be predetermined.
You can specify how many layers will be rotated by a certain angle, and according to which mathematical law this rotation will be performed. This law is determined by writing a certain symbol and specifying a numeric value after it. 
The following signs are available that determine the shape of the turn: `/` `#` `#-` `|` `N` `n` `Z` `z` `L` `l` `U` `u` `Q` `q` `$` `~` `^`. For their purpose, see [above](###Joint-sign).

Also, after the digital value there is a range modifier, then this rotation will occur according to the described length. 
The following modifiers are available that determine the range of turn: `mm` `cm` `m` `'` `"` `#` `%`. For their purpose, see [above](###Length-modifier).

If there is the `-` sign before the numeric value, then the initial fill angle changes with the final one. This is useful for joining the linear infills in some cases. Absolute values of the rotation angle using the range instructions have no effect.
It is important to know that this will not be the exact length, but will be tied to the nearest layer from below.

`+45/100` - rotate the next 100 layers linearly at a 45 degree angle. For this model, this instruction is equivalent of `+45/100%` as it contains 100 layers.<br/>
<img width="160" height="160" align="bottom" alt="image" src="https://github.com/user-attachments/assets/8606993c-0843-4952-8bb0-685c3ee4d690" /><br/>

When changing the height of the instruction `+45/50` or `+45/50%` - the final angle will be 90, as the turn will occur twice.<br/>
<img width="160" height="160" align="bottom" alt="image" src="https://github.com/user-attachments/assets/78a4cba0-b5ec-4c92-8f6d-9ef030331aa7" /><br/>

`-50%Z1cm` - rotate one centimeter of infill by sinus function at a 180 degree CW.<br/>

### Constant layer number instructions
There are 2 letter signs `T` and `B` that can determine the number of shell layers on top and bottom of model. It is useful for calculating skipping this amount to align the fill, or inserting the required number of horizontal solid bulkheads.

`B!, +30` - skip the first shell layers from rotate, then fill with 30 degrees turn each layer<br/>

`+30/1cm, T` -  rotate one centimeter of infill linearity at a 30 degree, then skip the number of layers equal to the count of the upper shell layers without rotation.<br/>

### Solid layers into sparse infill instructions
The following instructions allow you to embed solid layers in a sparse fill. The following commands are available `D` `S` `O` `M` `R`. For their purpose, see [above](###Solid-sign). 

It is possible to combine them with the rotation method and layer number constant - `DT` `S/` `M#` `OB`... 

`#14, +15R` -  put the 14 layers of sparse infill when put one rectilinear layer of solid infill with 15 degree turn<br/>
<img width="160" height="160" align="bottom" alt="image" src="https://github.com/user-attachments/assets/7866fd2c-880e-45e8-be5f-0cafe685f29a" /><br/>

`B!, 240M3, #25` - skip the first shell layers from rotate, fill model by 3 solid monotonic layers at 240 degree, then put 25 sparse layers by same angle<br/>

`+30/1cm, ST` -  rotate one centimeter of infill linearity at 30 degree, then put solid layers equal to the count of the top shell layers<br/>

`+30M3` or `+90M/3` -  fill whole model by solid infill with 30 degree turn at each layer<br/>

## Complex template examples
`+10L25%, -10l25%, -10L25%, +10l25%` -  fill the model with sine period with 10 degrees amplitude<br/>
<img width="160" height="160" align="bottom" alt="image" src="https://github.com/user-attachments/assets/de4bc426-3f73-4c98-a331-f237502f889f" /><br/>

`+30/-10#` - rotate the infill at height of 10 standard layers (or @ standard layer height is 0.2mm x 10 = 2mm) inverse linearly at a 30 degree angle.<br/>
<img width="160" height="160" align="bottom" alt="image" src="https://github.com/user-attachments/assets/e517f545-9690-4b3f-8d35-91068c63066e" /><br/>

`+360~100%` or `+100%~100%` - fill the model an infill with random direction at each layer.<br/>
<img width="160" height="160" align="bottom" alt="image" src="https://github.com/user-attachments/assets/20cd443f-ce90-4c51-923c-f0e3072d5a79" /><br/>
