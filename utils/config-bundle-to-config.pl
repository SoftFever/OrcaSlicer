#!/usr/bin/perl
# This script extracts a full active config from a config bundle.
# (Often users reporting issues don't attach plain configs, but 
# bundles...)

use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Getopt::Long qw(:config no_auto_abbrev);
use Slic3r;
use Slic3r::Test;
$|++;

my %opt = ();
{
    my %options = (
        'help'                  => sub { usage() },
        'output=s'              => \$opt{output},
    );
    GetOptions(%options) or usage(1);
    $ARGV[0] or usage(1);
}

($ARGV[0] && $opt{output}) or usage(1);

{
    my $bundle_ini = Slic3r::Config->read_ini($ARGV[0])
        or die "Failed to read $ARGV[0]\n";
    
    my $config_ini = { _ => {} };
    foreach my $section (qw(print filament printer)) {
        my $preset_name = $bundle_ini->{presets}{$section};
        $preset_name =~ s/\.ini$//;
        my $preset = $bundle_ini->{"$section:$preset_name"}
            or die "Failed to find preset $preset_name in bundle\n";
        $config_ini->{_}{$_} = $preset->{$_} for keys %$preset;
    }
    
    Slic3r::Config->write_ini($opt{output}, $config_ini);
}


sub usage {
    my ($exit_code) = @_;
    
    print <<"EOF";
Usage: config-bundle-to-config.pl --output config.ini bundle.ini
EOF
    exit ($exit_code || 0);
}

__END__
