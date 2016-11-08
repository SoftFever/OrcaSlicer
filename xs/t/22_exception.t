#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 1;

if ($ENV{SLIC3R_HAS_BROKEN_CROAK}) 
{
    ok 1, 'SLIC3R_HAS_BROKEN_CROAK set, croaks and confesses from a C++ code will lead to an application exit!';
}
else
{
    eval {
        Slic3r::xspp_test_croak_hangs_on_strawberry();
    };
    is $@, "xspp_test_croak_hangs_on_strawberry: exception catched\n", 'croak from inside a C++ exception delivered';
}

__END__
