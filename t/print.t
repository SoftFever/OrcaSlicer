use Test::More tests => 1;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw(first);
use Slic3r;
use Slic3r::Test;

{
    my $print = Slic3r::Test::init_print('20mm_cube', rotation => 45);
    ok !(first { $_ < 0 } map @$_, map @{$_->vertices}, grep $_, map @{$_->meshes}, @{$print->objects}),
        "object is still in positive coordinate space even after rotation";
}

__END__
