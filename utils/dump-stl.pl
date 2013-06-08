#!/usr/bin/perl
# This script dumps a STL file into Perl syntax for writing tests

use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
$|++;

$ARGV[0] or usage(1);

{
    my $model = Slic3r::Format::STL->read_file($ARGV[0]);
    my $mesh = $model->mesh;
    printf "VERTICES = %s\n", join ',', map "[$_->[0],$_->[1],$_->[2]]", @{$mesh->vertices};
    printf "FACETS = %s\n", join ',', map "[$_->[0],$_->[1],$_->[2]]", @{$mesh->facets};
}


sub usage {
    my ($exit_code) = @_;
    
    print <<"EOF";
Usage: dump-stl.pl file.stl
EOF
    exit ($exit_code || 0);
}

__END__
