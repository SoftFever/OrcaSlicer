# Introduction

Libnest2D is a library and framework for the 2D bin packaging problem. 
Inspired from the [SVGNest](svgnest.com) Javascript library the project is is 
built from scratch in C++11. The library is written with a policy that it should
be usable out of the box with a very simple interface but has to be customizable
to the very core as well. This has led to a design where the algorithms are 
defined in a header only fashion with template only geometry types. These 
geometries can have custom or already existing implementation to avoid copying 
or having unnecessary dependencies.

A default backend is provided if a user just wants to use the library out of the
box without implementing the interface of these geometry types. The default 
backend is built on top of boost geometry and the 
[polyclipping](http://www.angusj.com/delphi/clipper.php) library and implies the
dependency on these packages as well as the compilation of the backend (although
I may find a solution in the future to make the backend header only as well).

This software is currently under heavy construction and lacks a throughout 
documentation and some essential algorithms as well. At this point a fairly 
untested version of the DJD selection heuristic is working with a bottom-left 
placing strategy which may produce usable arrangements in most cases. 

The no-fit polygon based placement strategy will be implemented in the very near
future which should produce high quality results for convex and non convex 
polygons with holes as well.  

# References
- [SVGNest](https://github.com/Jack000/SVGnest)
- [An effective heuristic for the two-dimensional irregular
bin packing problem](http://www.cs.stir.ac.uk/~goc/papers/EffectiveHueristic2DAOR2013.pdf)
- [Complete and robust no-fit polygon generation for the irregular stock cutting problem](https://www.sciencedirect.com/science/article/abs/pii/S0377221706001639)
- [Applying Meta-Heuristic Algorithms to the Nesting
Problem Utilising the No Fit Polygon](http://www.graham-kendall.com/papers/k2001.pdf)
- [A comprehensive and robust procedure for obtaining the nofit polygon
using Minkowski sums](https://www.sciencedirect.com/science/article/pii/S0305054806000669)