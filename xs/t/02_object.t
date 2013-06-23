#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 1;

my $table = Slic3r::Object::XS::ZTable->new([ 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 ]);
is_deeply $table->get_range(39, 69), [2, 6], 'get_layer_range';

__END__
