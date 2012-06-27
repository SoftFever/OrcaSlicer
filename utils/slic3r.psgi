#!/usr/bin/perl
use strict;
use warnings;
use File::Temp qw/tempfile/;
use Plack::Request;
use Slic3r;

# Usage: perl -Ilib utils/slic3r.psgi
# Then point your browser at http://<ip:5000>/ or use:
#   curl -o thing.gcode -F stlfile=@/path/to/thing.stl \
#     http://127.0.0.1:5000/gcode

use constant {
  CONFIG => $ENV{SLIC3R_CONFIG},
  DEBUG => $ENV{SLIC3R_PSGI_DEBUG},
};

unless (caller) {
  require Plack::Runner;
  Plack::Runner->run(@ARGV, $0);
}

if (defined CONFIG) {
  Slic3r::Config->load(CONFIG);
} else {
  print "Using defaults as environment variable SLIC3R_CONFIG is not set\n";
}

Slic3r::Config->validate;

my $form;
{
  local $/;
  $form = <DATA>
};

my $app = sub {
  my $env = shift;
  my $req = Plack::Request->new($env);
  my $path_info = $req->path_info;
  if ($path_info =~ m!^/gcode!) {
    my $upload = $req->uploads->{'stlfile'}->path;
    rename $upload, $upload.'.stl';
    $upload .= '.stl';
    my $print = Slic3r::Print->new;
    $print->add_object_from_file($upload);
    if (0) { #$opt{merge}) {
      $print->add_object_from_file($_) for splice @ARGV, 0;
    }
    $print->duplicate;
    $print->arrange_objects if @{$print->objects} > 1;
    $print->validate;
    my ($fh, $filename) = tempfile();
    $print->export_gcode(output_file => $filename,
                         status_cb => sub {
                           my ($percent, $message) = @_;
                           printf "=> $message\n";
                         });
    unlink $filename;
    return [ 200, ['Content-Type' => 'text/plain'], $fh ];
  } else {
    return [ 200, ['Content-Type' => 'text/html'], [$form] ];
  }
};

__DATA__
<html>
<head>
  <title>Slic3r</title>
</head>
<body>
<h1>Slic3r</h1>
<form method="POST" action="gcode">
  <p>Select an stl file:
  <input type="file" name="stlfile" />
  </p>
  <input type="submit" value="Slice!" />
</form>
</body>
</html>

