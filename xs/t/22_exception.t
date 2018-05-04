#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 1;

eval {
    Slic3r::xspp_test_croak_hangs_on_strawberry();
};
is $@, "xspp_test_croak_hangs_on_strawberry: exception catched\n", 'croak from inside a C++ exception delivered';

__END__
