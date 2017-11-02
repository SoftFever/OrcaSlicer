use Test::More tests => 2;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
    use local::lib "$FindBin::Bin/../local-lib";
}

use List::Util qw(first);
use Slic3r;
use Slic3r::Test;

{
    my $print = Slic3r::Test::init_print('20mm_cube');
    {
        my $thread = threads->create(sub { Slic3r::thread_cleanup(); return 1; });
        ok $thread->join, "print survives thread spawning";
    }
}
    
{
    my $thread = threads->create(sub {
        {
            my $print = Slic3r::Test::init_print('20mm_cube');
            Slic3r::Test::gcode($print);
        }
        Slic3r::thread_cleanup();
        return 1;
    });
    ok $thread->join, "process print in a separate thread";
}

__END__
