#!/usr/bin/perl -i

#
# Post-processing script for calculating flow rate for each move

use strict;
use warnings;

use constant PI => 3.141592653589793238;
my @filament_diameter = split /,/, $ENV{SLIC3R_FILAMENT_DIAMETER};

my $E = 0;
my $T = 0;
my ($X, $Y, $F);
while (<>) {
    if (/^G1.*? F([0-9.]+)/) {
        $F = $1;
    }
    if (/^G1 X([0-9.]+) Y([0-9.]+).*? E([0-9.]+)/) {
        my ($x, $y, $e) = ($1, $2, $3);
        my $e_length = $e - $E;
        if ($e_length > 0 && defined $X && defined $Y) {
            my $dist = sqrt( (($x-$X)**2) + (($y-$Y)**2) );
            if ($dist > 0) {
                my $mm_per_mm   = $e_length / $dist;  # dE/dXY
                my $mm3_per_mm  = ($filament_diameter[$T] ** 2) * PI/4 * $mm_per_mm;
                my $vol_speed   = $F/60 * $mm3_per_mm;
                my $comment = sprintf ' ; dXY = %.3fmm ; dE = %.5fmm ; dE/XY = %.5fmm/mm; volspeed = %.5fmm\x{00B3}/sec',
                    $dist, $e_length, $mm_per_mm, $vol_speed;
                s/(\R+)/$comment$1/;
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
    if (/^T(\d+)/) {
        $T = $1;
    }
    print;
}

__END__
