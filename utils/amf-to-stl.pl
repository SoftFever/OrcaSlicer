#!/usr/bin/perl
# This script converts an AMF file to STL

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

# Convert all parameters from the local code page to utf8 on Windows.
@ARGV = map Slic3r::decode_path($_), @ARGV if $^O eq 'MSWin32';

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
    my $model = Slic3r::Model->load_amf($ARGV[0]);
    my $output_file = $ARGV[0];
    $output_file =~ s/\.[aA][mM][fF](?:\.[xX][mM][lL])?$/\.stl/;
    
    printf "Writing to %s\n", basename($output_file);
    $model->store_stl($output_file, binary => !$opt{ascii});
}


sub usage {
    my ($exit_code) = @_;
    
    print <<"EOF";
Usage: amf-to-stl.pl [ OPTIONS ] file.amf

    --help              Output this usage screen and exit
    --ascii             Generate ASCII STL files (default: binary)
    
EOF
    exit ($exit_code || 0);
}

__END__
