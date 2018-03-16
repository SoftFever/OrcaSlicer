use Test::More tests => 2;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
    use local::lib "$FindBin::Bin/../local-lib";
}

use Slic3r;
use Slic3r::Test;
use IO::Scalar;

{
    my $print = Slic3r::Test::init_print('20mm_cube');
    eval {
        my $fh = IO::Scalar->new(\my $gcode);
        $print->print->export_svg(output_fh => $fh, quiet => 1);
        $fh->close;
    };
    die $@ if $@;
    ok !$@, 'successful SVG export';
}

{
    my $print = Slic3r::Test::init_print('two_hollow_squares');
    eval {
        my $fh = IO::Scalar->new(\my $gcode);
        $print->print->export_svg(output_fh => $fh, quiet => 1);
        $fh->close;
    };
    die $@ if $@;
    ok !$@, 'successful SVG export of object with two islands';
}

__END__
