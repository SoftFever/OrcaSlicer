# Introduction

Libnest2D is a library and framework for the 2D bin packaging problem. 
Inspired from the [SVGNest](svgnest.com) Javascript library the project is  
built from scratch in C++11. The library is written with a policy that it should
be usable out of the box with a very simple interface but has to be customizable
to the very core as well. The algorithms are defined in a header only fashion 
with templated geometry types. These geometries can have custom or already 
existing implementation to avoid copying or having unnecessary dependencies.

A default backend is provided if the user of the library just wants to use it 
out of the box without additional integration. This backend is reasonably 
fast and robust, being built on top of boost geometry and the 
[polyclipping](http://www.angusj.com/delphi/clipper.php) library. Usage of 
this default backend implies the dependency on these packages but its header 
only as well.

This software is currently under construction and lacks a throughout 
documentation and some essential algorithms as well. At this stage it works well
for rectangles and convex closed polygons without considering holes and 
concavities.

Holes and non-convex polygons will be usable in the near future as well. The 
no fit polygon based placer module combined with the first fit selection 
strategy is now used in the [Slic3r](https://github.com/prusa3d/Slic3r) 
application's arrangement feature. It uses local optimization techniques to find
the best placement of each new item based on some features of the arrangement.

In the near future I would like to use machine learning to evaluate the 
placements and (or) the order if items in which they are placed and see what 
results can be obtained. This is a different approach than that of SVGnest which 
uses genetic algorithms to find better and better selection orders. Maybe the 
two approaches can be combined as well.

# References
- [SVGNest](https://github.com/Jack000/SVGnest)
- [An effective heuristic for the two-dimensional irregular
bin packing problem](http://www.cs.stir.ac.uk/~goc/papers/EffectiveHueristic2DAOR2013.pdf)
- [Complete and robust no-fit polygon generation for the irregular stock cutting problem](https://www.sciencedirect.com/science/article/abs/pii/S0377221706001639)
- [Applying Meta-Heuristic Algorithms to the Nesting
Problem Utilising the No Fit Polygon](http://www.graham-kendall.com/papers/k2001.pdf)
- [A comprehensive and robust procedure for obtaining the nofit polygon
using Minkowski sums](https://www.sciencedirect.com/science/article/pii/S0305054806000669)