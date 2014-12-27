#!/usr/bin/env perl

use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;

die "Usage: send-gcode.pl SERIALPORT BAUDRATE GCODE_FILE\n"
    if @ARGV != 3;

my $serial = Slic3r::GCode::Sender->new($ARGV[0], $ARGV[1]);
1 until $serial->is_connected;
print "Connected to printer\n";

{
    local $/ = "\n";
    Slic3r::open(\my $fh, '<', $ARGV[2])
        or die "Unable to open $ARGV[2]: $!\n";
    binmode $fh, ':utf8';
    while (<$fh>) {
        $serial->send($_);
    }
    close $fh;
}

while ((my $queue_size = $serial->queue_size) > 0) {
    printf "Queue size: %d\n", $queue_size;
}
$serial->disconnect;

__END__
