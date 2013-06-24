#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 1;

is Slic3r::TriangleMesh::XS::hello_world(), 'Hello world!',
    'hello world';

__END__
