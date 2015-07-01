#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 2;

{
    my $gcodegen = Slic3r::GCode->new;
    $gcodegen->set_origin(Slic3r::Pointf->new(10,0));
    is_deeply $gcodegen->origin->pp, [10,0], 'set_origin';
    $gcodegen->origin->translate(5,5);
    is_deeply $gcodegen->origin->pp, [15,5], 'origin returns reference to point';
}

__END__
