#!/usr/bin/perl -i
#
# Post-processing script for adding weight of required filament to
# G-code output.

use strict;
use warnings;

# example densities, adjust according to filament specifications
use constant PLA => 1.25; # g/cm3
use constant ABS => 1.05; # g/cm3

while (<>) {
    if (/^(;\s+filament\s+used\s+=\s.*\((\d+(?:\.\d+)?)cm3)\)/) {
        my $pla = $2 * PLA;
        my $abs = $2 * ABS;
        printf "%s or %.2fg PLA/%.2fg ABS)\n", $1, $pla, $abs;
    } else {
        print;
    }
}
