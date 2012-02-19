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
    my $mesh = Slic3r::STL->read_file($ARGV[0]);
    my $output_file = $ARGV[0];
    $output_file =~ s/\.stl$/.amf.xml/i;
    
    printf "Writing to %s\n", basename($output_file);
    Slic3r::AMF->write_file($output_file, $mesh);
}


sub usage {
    my ($exit_code) = @_;
    
    print <<"EOF";
Usage: amf-to-stl.pl [ OPTIONS ] file.stl

    --help              Output this usage screen and exit
    
EOF
    exit ($exit_code || 0);
}

__END__
