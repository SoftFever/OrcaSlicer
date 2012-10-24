#!/usr/bin/perl -i

use strict;
use warnings;

my $z = 0;

# read stdin and any/all files passed as parameters one line at a time
while (<>) {
	# if we find a Z word, save it
	$z = $1 if /Z\s*(\d+(\.\d+)?)/;

	# if we don't have Z, but we do have X and Y
	if (!/Z/ && /X/ && /Y/ && $z > 0) {
		# chop off the end of the line (incl. comments), saving chopped section in $1
		s/\s*([\r\n\;\(].*)/" Z$z $1"/es;
		# print start of line, insert our Z value then re-add the chopped end of line
		# print "$_ Z$z $1";
	}
	#else {
		# nothing interesting, print line as-is
	print or die $!;
	#}
}
