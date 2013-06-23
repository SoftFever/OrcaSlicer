#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 1;

is_deeply Slic3r::Object::XS::get_layer_range([ 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 ], 39, 69),
    [2, 6], 'get_layer_range';

__END__
