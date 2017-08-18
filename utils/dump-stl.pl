#!/usr/bin/perl
# This script dumps a STL file into Perl syntax for writing tests
# or dumps a test model into a STL file

use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
    use local::lib "$FindBin::Bin/../local-lib";
}

use Slic3r;
use Slic3r::Test;
use File::Basename qw(basename);
$|++;

$ARGV[0] or usage(1);

if (-e $ARGV[0]) {
    my $model = Slic3r::Model->load_stl($ARGV[0], basename($ARGV[0]));
    $model->objects->[0]->add_instance(offset => Slic3r::Pointf->new(0,0));
    my $mesh = $model->mesh;
    $mesh->repair;
    printf "VERTICES = %s\n", join ',', map "[$_->[0],$_->[1],$_->[2]]", @{$mesh->vertices};
    printf "FACETS = %s\n", join ',', map "[$_->[0],$_->[1],$_->[2]]", @{$mesh->facets};
    exit 0;
} elsif ((my $model = Slic3r::Test::model($ARGV[0]))) {
    $ARGV[1] or die "Missing writeable destination as second argument\n";
    $model->store_stl($ARGV[1], 1);
    printf "Model $ARGV[0] written to $ARGV[1]\n";
    exit 0;
} else {
    die "No such model exists\n";
}


sub usage {
    my ($exit_code) = @_;
    
    print <<"EOF";
Usage: dump-stl.pl file.stl
       dump-stl.pl modelname file.stl
EOF
    exit ($exit_code || 0);
}

__END__
