#!/usr/bin/perl
#
# Example post-processing script for sending a Prowl notification upon
# completion. See http://www.prowlapp.com/ for more info.

use strict;
use warnings;

use File::Basename qw(basename);
use WebService::Prowl;

# set your Prowl API key here
my $apikey = '';

my $file = basename $ARGV[0];
my $prowl = WebService::Prowl->new(apikey => $apikey);
my %options = (application => 'Slic3r',
               event =>'Slicing Done!',
               description => "$file was successfully generated");
printf STDERR "Error sending Prowl notification: %s\n", $prowl->error
    unless $prowl->add(%options);
