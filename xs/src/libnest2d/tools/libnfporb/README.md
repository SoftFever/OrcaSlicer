[![License: GPL v3](https://img.shields.io/badge/License-GPL%20v3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.en.html)
##### If you give me a real good reason i might be willing to give you permission to use it under a different license for a specific application. Real good reasons include the following (non-exhausive): the greater good, educational purpose and money :)

# libnfporb
Implementation of a robust no-fit polygon generation in a C++ library using an orbiting approach.

__Please note:__ The paper this implementation is based it on has several bad assumptions that required me to "improvise". That means the code doesn't reflect the paper anymore and is running way slower than expected. At the moment I'm working on implementing a new approach based on this paper (using minkowski sums): https://eprints.soton.ac.uk/36850/1/CORMSIS-05-05.pdf

## Description

The no-fit polygon optimization makes it possible to check for overlap (or non-overlapping touch) of two polygons with only 1 point in polygon check (by providing the set of non-overlapping placements).
This library implements the orbiting approach to generate the no-fit polygon: Given two polygons A and B, A is the stationary one and B the orbiting one, B is slid as tightly as possibly around the edges of polygon A. During the orbiting a chosen reference point is tracked. By tracking the movement of the reference point a third polygon can be generated: the no-fit polygon.

Once the no-fit polygon has been generated it can be used to test for overlap by only checking if the reference point is inside the NFP (overlap) outside the NFP (no overlap) or exactly on the edge of the NFP (touch).

### Examples:

The polygons: 

![Start of NFP](/images/start.png?raw=true)

Orbiting:

![State 1](/images/next0.png?raw=true)
![State 2](/images/next1.png?raw=true)
![State 3](/images/next2.png?raw=true)
![State 4](/images/next3.png?raw=true)

![State 5](/images/next4.png?raw=true)
![State 6](/images/next5.png?raw=true)
![State 7](/images/next6.png?raw=true)
![State 8](/images/next7.png?raw=true)

![State 9](/images/next8.png?raw=true)

The resulting NFP is red:

![nfp](/images/nfp.png?raw=true)

Polygons can have concavities, holes, interlocks or might fit perfectly:

![concavities](/images/concavities.png?raw=true)
![hole](/images/hole.png?raw=true)
![interlock](/images/interlock.png?raw=true)
![jigsaw](/images/jigsaw.png?raw=true)

## The Approach
The approch of this library is highly inspired by the scientific paper [Complete and robust no-fit polygon generation
for the irregular stock cutting problem](https://pdfs.semanticscholar.org/e698/0dd78306ba7d5bb349d20c6d8f2e0aa61062.pdf) and by [Svgnest](http://svgnest.com)

Note that is wasn't completely possible to implement it as suggested in the paper because it had several shortcomings that prevent complete NFP generation on some of my test cases. Especially the termination criteria (reference point returns to first point of NFP) proved to be wrong (see: test-case rect). Also tracking of used edges can't be performed as suggested in the paper since there might be situations where no edge of A is traversed (see: test-case doublecon).

By default the library is using floating point as coordinate type but by defining the flag "LIBNFP_USE_RATIONAL" the library can be instructed to use infinite precision.

## Build
The library has two dependencies: [Boost Geometry](http://www.boost.org/doc/libs/1_65_1/libs/geometry/doc/html/index.html) and [libgmp](https://gmplib.org). You need to install those first before building. Note that building is only required for the examples. The library itself is header-only.

    git clone https://github.com/kallaballa/libnfp.git
    cd libnfp
    make
    sudo make install

## Code Example

```c++
//uncomment next line to use infinite precision (slow)
//#define LIBNFP_USE_RATIONAL
#include "../src/libnfp.hpp"

int main(int argc, char** argv) {
  using namespace libnfp;
  polygon_t pA;
  polygon_t pB;
  //read polygons from wkt files
  read_wkt_polygon(argv[1], pA);
  read_wkt_polygon(argv[2], pB);

  //generate NFP of polygon A and polygon B and check the polygons for validity. 
  //When the third parameters is false validity check is skipped for a little performance increase
  nfp_t nfp = generateNFP(pA, pB, true);
  
  //write a svg containing pA, pB and NFP
  write_svg("nfp.svg",{pA,pB},nfp);
  return 0;
}
```
Run the example program:

    examples/nfp data/crossing/A.wkt data/crossing/B.wkt
