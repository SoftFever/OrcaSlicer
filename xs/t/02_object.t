#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 10;

my $table = Slic3r::Object::XS::ZTable->new([ 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 ]);
is_deeply $table->get_range(31, 61), [2, 6], 'get_layer_range';
is_deeply $table->get_range(39, 69), [2, 6], 'get_layer_range';
is_deeply $table->get_range(30, 60), [2, 5], 'get_layer_range';

# upper_bound points to the first element that is greater than argument
is $table->upper_bound(30), 3, 'upper_bound';
is $table->upper_bound(31), 3, 'upper_bound';
is $table->upper_bound(39), 3, 'upper_bound';
is $table->upper_bound(39, 4), 4, 'upper_bound with offset';

# lower_bound points to the first element that is not less than argument
is $table->lower_bound(31), 3, 'lower_bound';
is $table->lower_bound(39), 3, 'lower_bound';
is $table->lower_bound(40), 3, 'lower_bound';

__END__
