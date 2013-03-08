package Slic3r::Format::AMF;
use Moo;

use Slic3r::Geometry qw(X Y Z);

sub read_file {
    my $self = shift;
    my ($file) = @_;
    
    eval qq{
    	require Slic3r::Format::AMF::Parser;
    	use XML::SAX::ParserFactory;
    	1;
    } or die "AMF parsing requires XML::SAX\n";
    
    Slic3r::open(\my $fh, '<', $file) or die "Failed to open $file\n";
    
    my $model = Slic3r::Model->new;
    XML::SAX::ParserFactory
        ->parser(Handler => Slic3r::Format::AMF::Parser->new(_model => $model))
        ->parse_file($fh);
    close $fh;
    
    return $model;
}

sub write_file {
    my $self = shift;
    my ($file, $model, %params) = @_;
    
    my %vertices_offset = ();
    
    Slic3r::open(\my $fh, '>', $file);
    binmode $fh, ':utf8';
    printf $fh qq{<?xml version="1.0" encoding="UTF-8"?>\n};
    printf $fh qq{<amf unit="millimeter">\n};
    printf $fh qq{  <metadata type="cad">Slic3r %s</metadata>\n}, $Slic3r::VERSION;
    for my $material_id (sort keys %{ $model->materials }) {
        my $material = $model->materials->{$material_id};
        printf $fh qq{  <material id="%d">\n}, $material_id;
        for (keys %{$material->attributes}) {
             printf $fh qq{    <metadata type=\"%s\">%s</metadata>\n}, $_, $material->attributes->{$_};
        }
        printf $fh qq{  </material>\n};
    }
    my $instances = '';
    for my $object_id (0 .. $#{ $model->objects }) {
        my $object = $model->objects->[$object_id];
        printf $fh qq{  <object id="%d">\n}, $object_id;
        printf $fh qq{    <mesh>\n};
        printf $fh qq{      <vertices>\n};
        foreach my $vertex (@{$object->vertices}, ) {
            printf $fh qq{        <vertex>\n};
            printf $fh qq{          <coordinates>\n};
            printf $fh qq{            <x>%s</x>\n}, $vertex->[X];
            printf $fh qq{            <y>%s</y>\n}, $vertex->[Y];
            printf $fh qq{            <z>%s</z>\n}, $vertex->[Z];
            printf $fh qq{          </coordinates>\n};
            printf $fh qq{        </vertex>\n};
        }
        printf $fh qq{      </vertices>\n};
        foreach my $volume (@{ $object->volumes }) {
            printf $fh qq{      <volume%s>\n},
                (!defined $volume->material_id) ? '' : (sprintf ' materialid="%s"', $volume->material_id);
            foreach my $facet (@{$volume->facets}) {
                printf $fh qq{        <triangle>\n};
                printf $fh qq{          <v%d>%d</v%d>\n}, (4+$_), $facet->[$_], (4+$_) for -3..-1;
                printf $fh qq{        </triangle>\n};
            }
            printf $fh qq{      </volume>\n};
        }
        printf $fh qq{    </mesh>\n};
        printf $fh qq{  </object>\n};
        if ($object->instances) {
            foreach my $instance (@{$object->instances}) {
                $instances .= sprintf qq{    <instance objectid="%d">\n}, $object_id;
                $instances .= sprintf qq{      <deltax>%s</deltax>\n}, $instance->offset->[X];
                $instances .= sprintf qq{      <deltay>%s</deltay>\n}, $instance->offset->[Y];
                $instances .= sprintf qq{      <rz>%s</rz>\n}, $instance->rotation;
                $instances .= sprintf qq{    </instance>\n};
            }
        }
    }
    if ($instances) {
        printf $fh qq{  <constellation id="1">\n};
        printf $fh $instances;
        printf $fh qq{  </constellation>\n};
    }
    printf $fh qq{</amf>\n};
    close $fh;
}

1;
