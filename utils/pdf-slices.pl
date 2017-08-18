#!/usr/bin/perl
# This script exports model slices to a PDF file as solid fills, one per page

use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
    use local::lib "$FindBin::Bin/../local-lib";
}

use Getopt::Long qw(:config no_auto_abbrev);
use PDF::API2;
use Slic3r;
use Slic3r::Geometry qw(scale unscale X Y);

use constant mm => 25.4 / 72;

my %opt = ();
{
    my %options = (
        'help'                  => sub { usage() },
        'output|o=s'            => \$opt{output_file},
        'layer-height|h=f'      => \$opt{layer_height},
    );
    GetOptions(%options) or usage(1);
    $ARGV[0] or usage(1);
}

{
    # prepare config
    my $config = Slic3r::Config->new;
    $config->set('layer_height', $opt{layer_height}) if $opt{layer_height};
    
    # read model
    my $model = Slic3r::Model->read_from_file(my $input_file = $ARGV[0]);
    
    # init print object
    my $sprint = Slic3r::Print::Simple->new(
        print_center => [0,0],
    );
    $sprint->apply_config($config);
    $sprint->set_model($model);
    my $print = $sprint->_print;
    
    # compute sizes
    my $bb = $print->bounding_box;
    my $size = $bb->size;
    my $mediabox = [ map unscale($_)/mm, @{$size} ];
    
    # init PDF
    my $pdf = PDF::API2->new();
    my $color = $pdf->colorspace_separation('RDG_GLOSS', 'darkblue');
    
    # slice and build output geometry
    $_->slice for @{$print->objects};
    foreach my $object (@{ $print->objects }) {
        my $shift = $object->_shifted_copies->[0];
        $shift->translate(map $_/2, @$size);
        
        foreach my $layer (@{ $object->layers }) {
            my $page = $pdf->page();
            $page->mediabox(@$mediabox);
            my $content = $page->gfx;
            $content->fillcolor($color, 1);
        
            foreach my $expolygon (@{$layer->slices}) {
                $expolygon = $expolygon->clone;
                $expolygon->translate(@$shift);
                $content->poly(map { unscale($_->x)/mm, unscale($_->y)/mm } @{$expolygon->contour});  #)
                $content->close;
                foreach my $hole (@{$expolygon->holes}) {
                    $content->poly(map { unscale($_->x)/mm, unscale($_->y)/mm } @$hole);  #)
                    $content->close;
                }
                $content->fill;  # non-zero by default
            }
        }
    }
    
    # write output file
    my $output_file = $opt{output_file};
    if (!defined $output_file) {
        $output_file = $input_file;
        $output_file =~ s/\.(?:[sS][tT][lL])$/.pdf/;
    }
    $pdf->saveas($output_file);
    printf "PDF file written to %s\n", $output_file;
}

sub usage {
    my ($exit_code) = @_;
    
    print <<"EOF";
Usage: pdf-slices.pl [ OPTIONS ] file.stl

    --help              Output this usage screen and exit
    --output, -o        Write to the specified file
    --layer-height, -h  Use the specified layer height
    
EOF
    exit ($exit_code || 0);
}

__END__
