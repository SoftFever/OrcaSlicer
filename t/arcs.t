use Test::More;
use strict;
use warnings;

plan tests => 2;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;

my $path = Slic3r::ExtrusionPath->cast([
    [135322.42,26654.96], [187029.11,99546.23], [222515.14,92381.93], [258001.16,99546.23], 
    [286979.42,119083.91], [306517.1,148062.17], [313681.4,183548.2],
    [306517.1,219034.23], [286979.42,248012.49], [258001.16,267550.17], [222515.14,274714.47], 
    [187029.11,267550.17], [158050.85,248012.49], [138513.17,219034.23], [131348.87,183548.2], 
    [86948.77,175149.09], [119825.35,100585],
]);
my $collection = Slic3r::ExtrusionPath::Collection->new(paths => [$path]);
$collection->detect_arcs;

is scalar(@{$collection->paths}), 3, 'path collection now contains three paths';
isa_ok $collection->paths->[1], 'Slic3r::ExtrusionPath::Arc', 'second one';
