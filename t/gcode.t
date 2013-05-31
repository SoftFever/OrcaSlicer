use Test::More tests => 1;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Geometry qw(scale);

{
    my $gcodegen = Slic3r::GCode->new(
        config      => Slic3r::Config->new_from_defaults,
        layer_count => 1,
    );
    $gcodegen->set_shift(10, 10);
    is_deeply $gcodegen->last_pos, [scale -10, scale -10], 'last_pos is shifted correctly';
}

__END__
