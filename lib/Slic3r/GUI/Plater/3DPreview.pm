package Slic3r::GUI::Plater::3DPreview;
use strict;
use warnings;
use utf8;

use Slic3r::Print::State ':steps';
use Wx qw(:misc :sizer :slider :statictext wxWHITE);
use Wx::Event qw(EVT_SLIDER EVT_KEY_DOWN);
use base qw(Wx::Panel Class::Accessor);

__PACKAGE__->mk_accessors(qw(print enabled _loaded canvas slider_low slider_high));

sub new {
    my $class = shift;
    my ($parent, $print) = @_;
    
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition);
    
    # init GUI elements
    my $canvas = Slic3r::GUI::3DScene->new($self);
    $self->canvas($canvas);
    my $slider_low = Wx::Slider->new(
        $self, -1,
        0,                              # default
        0,                              # min
        # we set max to a bogus non-zero value because the MSW implementation of wxSlider
        # will skip drawing the slider if max <= min:
        1,                              # max
        wxDefaultPosition,
        wxDefaultSize,
        wxVERTICAL | wxSL_INVERSE,
    );
    $self->slider_low($slider_low);
    my $slider_high = Wx::Slider->new(
        $self, -1,
        0,                              # default
        0,                              # min
        # we set max to a bogus non-zero value because the MSW implementation of wxSlider
        # will skip drawing the slider if max <= min:
        1,                              # max
        wxDefaultPosition,
        wxDefaultSize,
        wxVERTICAL | wxSL_INVERSE,
    );
    $self->slider_high($slider_high);
    
    my $z_label_low = $self->{z_label_low} = Wx::StaticText->new($self, -1, "", wxDefaultPosition,
        [40,-1], wxALIGN_CENTRE_HORIZONTAL);
    $z_label_low->SetFont($Slic3r::GUI::small_font);
    my $z_label_high = $self->{z_label_high} = Wx::StaticText->new($self, -1, "", wxDefaultPosition,
        [40,-1], wxALIGN_CENTRE_HORIZONTAL);
    $z_label_high->SetFont($Slic3r::GUI::small_font);
    
    my $hsizer = Wx::BoxSizer->new(wxHORIZONTAL);
    my $vsizer = Wx::BoxSizer->new(wxVERTICAL);
    $vsizer->Add($slider_low, 3, 0, 0);
    $vsizer->Add($z_label_low, 0, 0, 0);
    $hsizer->Add($vsizer, 0, wxEXPAND, 0);
    $vsizer = Wx::BoxSizer->new(wxVERTICAL);
    $vsizer->Add($slider_high, 3, 0, 0);
    $vsizer->Add($z_label_high, 0, 0, 0);
    $hsizer->Add($vsizer, 0, wxEXPAND, 0);
    
    my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
    $sizer->Add($canvas, 1, wxALL | wxEXPAND, 0);
    $sizer->Add($hsizer, 0, wxTOP | wxBOTTOM | wxEXPAND, 5);
    
    EVT_SLIDER($self, $slider_low, sub {
        if ($self->enabled) {
            my $idx_low  = $slider_low->GetValue;
            my $idx_high = $slider_high->GetValue;
            if ($idx_low >= $idx_high) {
                $idx_high = $idx_low;
                $self->slider_high->SetValue($idx_high);
            }
            $self->set_z_range($self->{layers_z}[$idx_low], $self->{layers_z}[$idx_high]);
        }
    });
    EVT_SLIDER($self, $slider_high, sub {
        if ($self->enabled) {
            my $idx_low  = $slider_low->GetValue;
            my $idx_high = $slider_high->GetValue;
            if ($idx_low > $idx_high) {
                $idx_low = $idx_high;
                $self->slider_low->SetValue($idx_low);
            }
            $self->set_z_range($self->{layers_z}[$idx_low], $self->{layers_z}[$idx_high]);
        }
    });
    EVT_KEY_DOWN($canvas, sub {
        my ($s, $event) = @_;
        
        my $key = $event->GetKeyCode;
        if ($key == 85 || $key == 315) {
            $slider_high->SetValue($slider_high->GetValue + 1);
            $self->set_z_high($self->{layers_z}[$slider_high->GetValue]);
        } elsif ($key == 68 || $key == 317) {
            $slider_high->SetValue($slider_high->GetValue - 1);
            $self->set_z_high($self->{layers_z}[$slider_high->GetValue]);
        }
    });
    
    $self->SetSizer($sizer);
    $self->SetMinSize($self->GetSize);
    $sizer->SetSizeHints($self);
    
    # init canvas
    $self->print($print);
    $self->reload_print;
    
    return $self;
}

sub reload_print {
    my ($self) = @_;
    
    $self->canvas->reset_objects;
    $self->_loaded(0);
    $self->load_print;
}

sub load_print {
    my ($self) = @_;
    
    return if $self->_loaded;
    
    # we require that there's at least one object and the posSlice step
    # is performed on all of them (this ensures that _shifted_copies was
    # populated and we know the number of layers)
    if (!$self->print->object_step_done(STEP_SLICE)) {
        $self->enabled(0);
        $self->slider_low->Hide;
        $self->slider_high->Hide;
        $self->canvas->Refresh;  # clears canvas
        return;
    }
    
    my $z_idx;
    {
        my %z = ();  # z => 1
        foreach my $object (@{$self->{print}->objects}) {
            foreach my $layer (@{$object->layers}, @{$object->support_layers}) {
                $z{$layer->print_z} = 1;
            }
        }
        $self->enabled(1);
        $self->{layers_z} = [ sort { $a <=> $b } keys %z ];
        my $num_layers = scalar(@{$self->{layers_z}});
        $self->slider_low->SetRange(0, $num_layers-1);
        $self->slider_high->SetRange(0, $num_layers-1);
        $self->slider_low->SetValue(0);
        if (($z_idx = $self->slider_high->GetValue) < $num_layers && $self->slider_high->GetValue != 0) {
            # use $z_idx
        } else {
            $self->slider_high->SetValue($num_layers-1);
            $z_idx = $num_layers ? ($num_layers-1) : undef;
        }
        $self->slider_low->Show;
        $self->slider_high->Show;
        $self->Layout;
    }
    
    if ($self->IsShown) {
        # load skirt and brim
        $self->canvas->load_print_toolpaths($self->print);
        
        foreach my $object (@{$self->print->objects}) {
            $self->canvas->load_print_object_toolpaths($object);
            
            # Show the objects in very transparent color.
            #my @volume_ids = $self->canvas->load_object($object->model_object);
            #$self->canvas->volumes->[$_]->color->[3] = 0.2 for @volume_ids;
        }
        $self->canvas->zoom_to_volumes;
        $self->_loaded(1);
    }
    
    $self->set_z_range(0, $self->{layers_z}[$z_idx]);
}

sub set_z_range
{
    my ($self, $z_low, $z_high) = @_;
    
    return if !$self->enabled;
    $self->{z_label_low}->SetLabel(sprintf '%.2f', $z_low);
    $self->{z_label_high}->SetLabel(sprintf '%.2f', $z_high);
    $self->canvas->set_toolpaths_range($z_low-1e-5, $z_high+1e-5);
    $self->canvas->Refresh if $self->IsShown;
}

sub set_bed_shape {
    my ($self, $bed_shape) = @_;
    $self->canvas->set_bed_shape($bed_shape);
}

1;
