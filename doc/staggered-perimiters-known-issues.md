# Staggered layers is currently in *Alpha*.

![Icon](https://cdn.discordapp.com/attachments/1087393628420329585/1332429863180570735/staggered_layers_icon.png?ex=67953982&is=6793e802&hm=192dffcbf898bed5a9700a7cc6cc879413b1216aad2f0e97845a6d751f2be7be&)

---

## If you find an issue or encounter a bug that is not on this list, please let us know by either:
#### - Opening an issue on this GitHub repo
#### - Sending an Email to `vipul@createinc.dev`
#### - Sending a Direct Message on Discord to `Divide#4615` or `Xero#1678`

-----

# Known Issues:
- Staggared layers does not take in to account the slope of a wall, leading to `Inner-wall`s getting staggared even when visible from above ( [img1](https://cdn.discordapp.com/attachments/1314975632236609651/1332410916154773504/image.png?ex=679527dd&is=6793d65d&hm=683e08e6b6629974bc8bc11e14b9d0acd0e23335eafebeb43ede828c2acea57e&) | [img2](https://cdn.discordapp.com/attachments/1314975632236609651/1332410869975486484/image.png?ex=679527d2&is=6793d652&hm=8196be2e6b31e7202fb11f628ae4aa1d9deed9f4daa41bb414cecefff1a6448f&) )
- When `only_one_wall_first_layer` is **enabled**, the flowrate adjustment to correct for the lifted `Inner-wall`s is not correctly applied ( [img1](https://cdn.discordapp.com/attachments/1314975632236609651/1332410869975486484/image.png?ex=679527d2&is=6793d652&hm=8196be2e6b31e7202fb11f628ae4aa1d9deed9f4daa41bb414cecefff1a6448f&) | [img2](https://cdn.discordapp.com/attachments/1314975632236609651/1332422246735548478/image.png?ex=6795326a&is=6793e0ea&hm=02358016b0ae7c4d39cca76d1d926fe2cb2e3ee659517d519542c3c06a07471f&) )
- Having multiple models of different heights causes the check for the top layer to only work on the talest model (even if only 1 of the models has the setting enabled
- Orca slicer layer preview sees 1 layer as multiple different layers (usually 3, (lower walls, raised walls, infill). Sometimes 2, (lower walls and infill, raised walls)) (this is worsened if an object has 2 seperate sections of `outer-walls` as they get treated as seperate 'towers')
- Perimiters on internal holes are not staggered, only the outer most walls ( [img1](https://cdn.discordapp.com/attachments/1314975632236609651/1333403537425829898/image.png?ex=6798c450&is=679772d0&hm=dc87d94afa168c7ce6bdf75775669932daab1a034929302b3ff5c55c989beb83&) | [img2](https://cdn.discordapp.com/attachments/1314975632236609651/1333403854011629588/image.png?ex=6798c49c&is=6797731c&hm=2aa02f8e42fe59a4e954000a8d0f1912177b92234b947952dbda476a6ce67ca7&) )

# Incompatible settings: 
- `Adaptive Layer Height` is not currently supported
- `Initial_layer_print_height` & `Layer_height` must currently be the same
