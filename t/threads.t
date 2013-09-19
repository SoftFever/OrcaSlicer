use Test::More;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw(first);
use Slic3r;
use Slic3r::Test;

if (!$Slic3r::have_threads) {
    plan skip_all => "this perl is not compiled with threads";
}
plan tests => 2;

{
    my $print = Slic3r::Test::init_print('20mm_cube');
    {
        my $thread = threads->create(sub { Slic3r::thread_cleanup(); return 1; });
        ok $thread->join, "print survives thread spawning";
    }
}
    
{
    my $thread = threads->create(sub {
        # $print can't be inizialized outside the thread because Object->slice will
        # modify it by removing meshes and popping layers
        my $print = Slic3r::Test::init_print('20mm_cube');
        Slic3r::Test::gcode($print);
        Slic3r::thread_cleanup();
        return 1;
    });
    ok $thread->join, "process print in a separate thread";
}

__END__
