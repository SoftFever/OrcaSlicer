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
    );
    GetOptions(%options) or usage(1);
    $ARGV[0] or usage(1);
}

{
    my @meshes = map Slic3r::Format::STL->read_file($_), @ARGV;
    my $output_file = $ARGV[0];
    $output_file =~ s/\.stl$/.amf.xml/i;
    
    my $materials = {};
    my $meshes_by_material = {};
    if (@meshes == 1) {
        $meshes_by_material->{_} = $meshes[0];
    } else {
        for (0..$#meshes) {
            $materials->{$_+1} = { Name => basename($ARGV[$_]) };
            $meshes_by_material->{$_+1} = $meshes[$_];
        }
    }
    
    printf "Writing to %s\n", basename($output_file);
    Slic3r::Format::AMF->write_file($output_file, $materials, $meshes_by_material);
}


sub usage {
    my ($exit_code) = @_;
    
    print <<"EOF";
Usage: amf-to-stl.pl [ OPTIONS ] file.stl [ file2.stl [ file3.stl ] ]

    --help              Output this usage screen and exit
    
EOF
    exit ($exit_code || 0);
}

__END__
