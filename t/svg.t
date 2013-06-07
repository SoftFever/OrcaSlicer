use Test::More tests => 1;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Test;

{
    my $print = Slic3r::Test::init_print('20mm_cube');
    eval {
        my $fh = IO::Scalar->new(\my $gcode);
        $print->export_svg(output_fh => $fh, quiet => 1);
        $fh->close;
    };
    ok !$@, 'successful SVG export';
}

__END__
