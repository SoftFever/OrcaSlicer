#!/usr/bin/perl
# This script splits a STL plate into individual files

use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use File::Basename qw(basename);
use Getopt::Long qw(:config no_auto_abbrev);
use Slic3r;
$|++;

my %opt = ();
{
    my %options = (
        'help'                  => sub { usage() },
        'ascii'                 => \$opt{ascii},
    );
    GetOptions(%options) or usage(1);
    $ARGV[0] or usage(1);
}

{
    my $mesh = Slic3r::STL->read_file($ARGV[0]);
    my $basename = $ARGV[0];
    $basename =~ s/\.stl$//i;
    
    # loop while we have remaining facets
    my $part_count = 0;
    while (1) {
        # get the first facet
        my @facet_queue = ();
        my @facets = ();
        for (my $i = 0; $i <= $#{$mesh->facets}; $i++) {
            if (defined $mesh->facets->[$i]) {
                push @facet_queue, $i;
                last;
            }
        }
        last if !@facet_queue;
        
        while (defined (my $facet_id = shift @facet_queue)) {
            next unless defined $mesh->facets->[$facet_id];
            push @facets, $mesh->facets->[$facet_id];
            push @facet_queue, $mesh->get_connected_facets($facet_id);
            $mesh->facets->[$facet_id] = undef;
        }
        
        my $output_file = sprintf '%s_%02d.stl', $basename, ++$part_count;
        printf "Writing to %s\n", basename($output_file);
        my $new_mesh = Slic3r::TriangleMesh->new(facets => \@facets, vertices => $mesh->vertices);
        Slic3r::STL->write_file($output_file, $new_mesh, !$opt{ascii});
    }
}


sub usage {
    my ($exit_code) = @_;
    
    print <<"EOF";
Usage: split_stl.pl [ OPTIONS ] file.stl

    --help              Output this usage screen and exit
    --ascii             Generate ASCII STL files (default: binary)
    
EOF
    exit ($exit_code || 0);
}

__END__
