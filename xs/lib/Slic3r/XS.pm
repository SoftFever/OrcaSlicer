package Slic3r::XS;
use warnings;
use strict;

our $VERSION = '0.01';

use XSLoader;
XSLoader::load(__PACKAGE__, $VERSION);

package Slic3r::Point::XS;
use overload
    '@{}' => sub { $_[0]->arrayref };

package Slic3r::ExPolygon::XS;
use overload
    '@{}' => sub { $_[0]->arrayref };

1;
