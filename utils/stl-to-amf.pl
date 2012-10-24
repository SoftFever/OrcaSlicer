#!/usr/bin/perl
# This script converts a STL file to AMF

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
        'distinct-materials'    => \$opt{distinct_materials},
    );
    GetOptions(%options) or usage(1);
    $ARGV[0] or usage(1);
}

{
    my @models = map Slic3r::Format::STL->read_file($_), @ARGV;
    my $output_file = $ARGV[0];
    $output_file =~ s/\.stl$/.amf.xml/i;
    
    my $new_model = Slic3r::Model->new;
    
    if ($opt{distinct_materials} && @models > 1) {
        my $new_object = $new_model->add_object;
        for my $m (0 .. $#models) {
            my $model = $models[$m];
            $new_model->set_material($m, { Name => basename($ARGV[$m]) });
            $new_object->add_volume(
                material_id => $m,
                facets      => $model->objects->[0]->volumes->[0]->facets,
                vertices    => $model->objects->[0]->vertices,
            );
        }
    } else {
        foreach my $model (@models) {
            $new_model->add_object(
                vertices => $model->objects->[0]->vertices,
            )->add_volume(
                facets => $model->objects->[0]->volumes->[0]->facets,
            );
        }
    }
    
    printf "Writing to %s\n", basename($output_file);
    Slic3r::Format::AMF->write_file($output_file, $new_model);
}


sub usage {
    my ($exit_code) = @_;
    
    print <<"EOF";
Usage: amf-to-stl.pl [ OPTIONS ] file.stl [ file2.stl [ file3.stl ] ]

    --help              Output this usage screen and exit
    --distinct-materials Assign each STL file to a different material
    
EOF
    exit ($exit_code || 0);
}

__END__
