#!/usr/bin/env perl

use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;

my $serial = Slic3r::GCode::Sender->new($ARGV[0], $ARGV[1]);

$serial->send($ARGV[2]);

exit;

while (1) {
    $serial->send("1");
    sleep 1;
    $serial->send("0");
    sleep 1;
}

__END__
