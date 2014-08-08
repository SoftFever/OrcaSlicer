#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 2;

{
    my $flow = Slic3r::Flow->new_from_width(
        role => Slic3r::Flow::FLOW_ROLE_PERIMETER,
        width               => '1',
        nozzle_diameter     => 0.5,
        layer_height        => 0.3, 
        bridge_flow_ratio   => 1,
    );
    isa_ok $flow, 'Slic3r::Flow', 'new_from_width';
}

{
    my $flow = Slic3r::Flow->new(
        width               => 1,
        height              => 0.4,
        nozzle_diameter     => 0.5,
    );
    isa_ok $flow, 'Slic3r::Flow', 'new';
}

__END__
