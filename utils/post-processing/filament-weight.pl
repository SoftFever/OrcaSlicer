#!/usr/bin/perl -i
#
# Post-processing script for adding weight and cost of required
# filament to G-code output.

use strict;
use warnings;

# example densities, adjust according to filament specifications
use constant PLA_P => 1.25; # g/cm3
use constant ABS_P => 1.05; # g/cm3

# example costs, adjust according to filament prices
use constant PLA_PRICE => 0.05; # EUR/g
use constant ABS_PRICE => 0.02; # EUR/g
use constant CURRENCY => "EUR";

while (<>) {
    if (/^(;\s+filament\s+used\s+=\s.*\((\d+(?:\.\d+)?)cm3)\)/) {
        my $pla_weight = $2 * PLA_P;
        my $abs_weight = $2 * ABS_P;

        my $pla_costs = $pla_weight * PLA_PRICE;
        my $abs_costs = $abs_weight * ABS_PRICE;

        printf "%s or %.2fg PLA/%.2fg ABS)\n", $1, $pla_weight, $abs_weight;
        printf "; costs = %s %.2f (PLA), %s %.2f (ABS)\n", CURRENCY, $pla_costs, CURRENCY, $abs_costs;
    } else {
        print;
    }
}
