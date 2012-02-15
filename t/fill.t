use Test::More;
use strict;
use warnings;

plan tests => 2;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;

my $print = Slic3r::Print->new(
    x_length => 50,
    y_length => 50,
);

{
    my $filler = Slic3r::Fill::Rectilinear->new(print => $print);
    my $surface_width = 250;
    my $distance = $filler->adjust_solid_spacing(
        width       => $surface_width,
        distance    => 100,
    );
    is $distance, 125, 'adjusted solid distance';
    is $surface_width % $distance, 0, 'adjusted solid distance';
}

__END__
