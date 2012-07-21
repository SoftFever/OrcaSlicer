use Test::More;
use strict;
use warnings;

plan tests => 2;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Geometry qw(scale);

#==========================================================

{
    my $points = [
        [226,701], [260,681], [109,420], [149,397], [300,658], [308,654],
    ];
    foreach my $point (@$points) {
        @$point = map scale $_, @$point;
    }
    my $polyline = Slic3r::Polyline->new($points);
    my $serialized = $polyline->serialize;
    my $deserialized = Slic3r::Polyline->deserialize($serialized);
    is scalar(@$deserialized), scalar(@$points), 'number of deserialized points';
    is_deeply $deserialized, $points, 'deserialized points coordinates';
}

#==========================================================

__END__
