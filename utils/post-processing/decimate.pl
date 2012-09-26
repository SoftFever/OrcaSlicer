#!/usr/bin/perl -i~

use strict;
use warnings;

my %lastpos = (X => 10000, Y => 10000, Z => 10000, E => 10000, F => 10000);
my %pos = (X => 0, Y => 0, Z => 0, E => 0, F => 0);

my $mindist = 0.33;

my $mindistz = 0.005;

my $mindistsq = $mindist * $mindist;

sub dist {
	my $sq = 0;
	for (qw/X Y Z E/) {
		$sq += ($pos{$_} - $lastpos{$_}) ** 2;
	}
	return $sq;
}

while (<>) {
	if (m#\bG[01]\b#) {
		while (m#([XYZEF])(\d+(\.\d+)?)#gi) {
			$pos{uc $1} = $2;
		}
		if (
				(
					/X/ &&
					/Y/ &&
					(dist() >= $mindistsq)
				) ||
				(abs($pos{Z} - $lastpos{Z}) > $mindistz) ||
				(!/X/ || !/Y/)
			) {
			print;
			%lastpos = %pos;
		}
		elsif (($pos{F} - $lastpos{F}) != 0) {
			printf "G1 F%s\n", $pos{F};
			$lastpos{F} = $pos{F};
		}
	}
	else {
		if (m#\bG92\b#) {
			while (m#([XYZEF])(\d+(\.\d+)?)#gi) {
				$lastpos{uc $1} = $2;
			}
		}
		print;
	}
}
