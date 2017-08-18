#!/usr/bin/perl
# This script splits a STL plate into individual files

use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
    use local::lib "$FindBin::Bin/../local-lib";
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
    my $model = Slic3r::Model->load_stl($ARGV[0], basename($ARGV[0]));
    my $basename = $ARGV[0];
    $basename =~ s/\.[sS][tT][lL]$//;
    
    my $part_count = 0;
    my $mesh = $model->objects->[0]->volumes->[0]->mesh;
    foreach my $new_mesh (@{$mesh->split}) {
        $new_mesh->repair;
        
        my $new_model = Slic3r::Model->new;
        $new_model
            ->add_object()
            ->add_volume(mesh => $new_mesh);
        
        $new_model->add_default_instances;
        
        my $output_file = sprintf '%s_%02d.stl', $basename, ++$part_count;
        printf "Writing to %s\n", basename($output_file);
        $new_model->store_stl($output_file, !$opt{ascii});
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
