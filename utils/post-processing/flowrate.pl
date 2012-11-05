#!/usr/bin/perl -i

#
# Post-processing script for calculating flow rate for each move

use strict;
use warnings;

my $E = 0;
my ($X, $Y);
while (<>) {
    if (/^G1 X([0-9.]+) Y([0-9.]+).*? E([0-9.]+)/) {
        my ($x, $y, $e) = ($1, $2, $3);
        my $e_length = $e - $E;
        if ($e_length > 0 && defined $X && defined $Y) {
            my $dist = sqrt( (($x-$X)**2) + (($y-$Y)**2) );
            if ($dist > 0) {
                my $flowrate = sprintf '%.2f', $e_length / $dist;
                s/(\R+)/ ; XY dist = $dist ; E dist = $e_length ; E\/XY = $flowrate mm\/mm$1/;
            }
        }
        $E = $e;
        $X = $x;
        $Y = $y;
    }
    if (/^G1 X([0-9.]+) Y([0-9.]+)/) {
        $X = $1;
        $Y = $2;
    }
    if (/^G1.*? E([0-9.]+)/) {
        $E = $1;
    }
    if (/^G92 E0/) {
        $E = 0;
    }
    print;
}

__END__
