#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 3;

{
    my $print = Slic3r::Print->new;
    isa_ok $print, 'Slic3r::Print';
    isa_ok $print->config, 'Slic3r::Config::Static::Ref';
    isa_ok $print->placeholder_parser, 'Slic3r::GCode::PlaceholderParser::Ref';
}

__END__
