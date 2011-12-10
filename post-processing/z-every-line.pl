#!/usr/bin/perl

use strict;

my $z = 0;

for (<>) {
	if (/Z(\d+(\.\d+)?)/) {
		$z = $1;
		print;
	}
	else {
		if (!/Z/ && /X/ && /Y/ && $z > 0) {
			s/\s*([\r\n\;\(].*)//gs;
			print "$_ Z$z $1";
		}
		else {
			print;
		}
	}
}
