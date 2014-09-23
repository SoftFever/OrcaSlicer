package Slic3r::GUI::ConfigWizard;
use strict;
use warnings;
use utf8;

use Wx;
use base 'Wx::Wizard';
use Slic3r::Geometry qw(unscale);

# adhere to various human interface guidelines
our $wizard = 'Wizard';
$wizard = 'Assistant' if &Wx::wxMAC || &Wx::wxGTK;

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, -1, "Configuration $wizard");

    # initialize an empty repository
    $self->{config} = Slic3r::Config->new;

    $self->add_page(Slic3r::GUI::ConfigWizard::Page::Welcome->new($self));
    $self->add_page(Slic3r::GUI::ConfigWizard::Page::Firmware->new($self));
    $self->add_page(Slic3r::GUI::ConfigWizard::Page::Bed->new($self));
    $self->add_page(Slic3r::GUI::ConfigWizard::Page::Nozzle->new($self));
    $self->add_page(Slic3r::GUI::ConfigWizard::Page::Filament->new($self));
    $self->add_page(Slic3r::GUI::ConfigWizard::Page::Temperature->new($self));
    $self->add_page(Slic3r::GUI::ConfigWizard::Page::BedTemperature->new($self));
    $self->add_page(Slic3r::GUI::ConfigWizard::Page::Finished->new($self));

    $_->build_index for @{$self->{pages}};

    return $self;
}

sub add_page {
    my $self = shift;
    my ($page) = @_;

    my $n = push @{$self->{pages}}, $page;
    # add first page to the page area sizer
    $self->GetPageAreaSizer->Add($page) if $n == 1;
    # link pages
    $self->{pages}[$n-2]->set_next_page($page) if $n >= 2;
    $page->set_previous_page($self->{pages}[$n-2]) if $n >= 2;
}

sub run {
    my $self = shift;
    
    if (Wx::Wizard::RunWizard($self, $self->{pages}[0])) {
        
        # it would be cleaner to have these defined inside each page class,
        # in some event getting called before leaving the page
        {
            # set first_layer_height + layer_height based on nozzle_diameter
            my $nozzle = $self->{config}->nozzle_diameter;
            $self->{config}->set('first_layer_height', $nozzle->[0]);
            $self->{config}->set('layer_height', $nozzle->[0] - 0.1);
            
            # set first_layer_temperature to temperature + 5
            $self->{config}->set('first_layer_temperature', [$self->{config}->temperature->[0] + 5]);
            
            # set first_layer_bed_temperature to temperature + 5
            $self->{config}->set('first_layer_bed_temperature',
                ($self->{config}->bed_temperature > 0) ? ($self->{config}->bed_temperature + 5) : 0);
        }
        
        $self->Destroy;
        return $self->{config};
    } else {
        $self->Destroy;
        return undef;
    }
}

package Slic3r::GUI::ConfigWizard::Index;
use Wx qw(:bitmap :dc :font :misc :sizer :systemsettings :window);
use Wx::Event qw(EVT_ERASE_BACKGROUND EVT_PAINT);
use base 'Wx::Panel';

sub new {
    my $class = shift;
    my ($parent, $title) = @_;
    my $self = $class->SUPER::new($parent);

    push @{$self->{titles}}, $title;
    $self->{own_index} = 0;

    $self->{bullets}->{before} = Wx::Bitmap->new("$Slic3r::var/bullet_black.png", wxBITMAP_TYPE_PNG);
    $self->{bullets}->{own}    = Wx::Bitmap->new("$Slic3r::var/bullet_blue.png",  wxBITMAP_TYPE_PNG);
    $self->{bullets}->{after}  = Wx::Bitmap->new("$Slic3r::var/bullet_white.png", wxBITMAP_TYPE_PNG);

    $self->{background} = Wx::Bitmap->new("$Slic3r::var/Slic3r_192px_transparent.png", wxBITMAP_TYPE_PNG);
    $self->SetMinSize(Wx::Size->new($self->{background}->GetWidth, $self->{background}->GetHeight));

    EVT_PAINT($self, \&repaint);

    return $self;
}

sub repaint {
    my ($self, $event) = @_;
    my $size = $self->GetClientSize;
    my $gap = 5;

    my $dc = Wx::PaintDC->new($self);
    $dc->SetBackgroundMode(wxTRANSPARENT);
    $dc->SetFont($self->GetFont);
    $dc->SetTextForeground($self->GetForegroundColour);

    my $background_h = $self->{background}->GetHeight;
    my $background_w = $self->{background}->GetWidth;
    $dc->DrawBitmap($self->{background}, ($size->GetWidth - $background_w) / 2, ($size->GetHeight - $background_h) / 2, 1);

    my $label_h = $self->{bullets}->{own}->GetHeight;
    $label_h = $dc->GetCharHeight if $dc->GetCharHeight > $label_h;
    my $label_w = $size->GetWidth;

    my $i = 0;
    foreach (@{$self->{titles}}) {
        my $bullet = $self->{bullets}->{own};
        $bullet = $self->{bullets}->{before} if $i < $self->{own_index};
        $bullet = $self->{bullets}->{after} if $i > $self->{own_index};

        $dc->SetTextForeground(Wx::Colour->new(128, 128, 128)) if $i > $self->{own_index};
        $dc->DrawLabel($_, $bullet, Wx::Rect->new(0, $i * ($label_h + $gap), $label_w, $label_h));
        $i++;
    }

    $event->Skip;
}

sub prepend_title {
    my $self = shift;
    my ($title) = @_;

    unshift @{$self->{titles}}, $title;
    $self->{own_index}++;
    $self->Refresh;
}

sub append_title {
    my $self = shift;
    my ($title) = @_;

    push @{$self->{titles}}, $title;
    $self->Refresh;
}

package Slic3r::GUI::ConfigWizard::Page;
use Wx qw(:font :misc :sizer :staticline :systemsettings);
use base 'Wx::WizardPage';

sub new {
    my $class = shift;
    my ($parent, $title, $short_title) = @_;
    my $self = $class->SUPER::new($parent);

    my $sizer = Wx::FlexGridSizer->new(0, 2, 10, 10);
    $sizer->AddGrowableCol(1, 1);
    $sizer->AddGrowableRow(1, 1);
    $sizer->AddStretchSpacer(0);
    $self->SetSizer($sizer);

    # title
    my $text = Wx::StaticText->new($self, -1, $title, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    my $bold_font = Wx::SystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    $bold_font->SetWeight(wxFONTWEIGHT_BOLD);
    $bold_font->SetPointSize(14);
    $text->SetFont($bold_font);
    $sizer->Add($text, 0, wxALIGN_LEFT, 0);

    # index
    $self->{short_title} = $short_title ? $short_title : $title;
    $self->{index} = Slic3r::GUI::ConfigWizard::Index->new($self, $self->{short_title});
    $sizer->Add($self->{index}, 1, wxEXPAND | wxTOP | wxRIGHT, 10);

    # contents
    $self->{width} = 430;
    $self->{vsizer} = Wx::BoxSizer->new(wxVERTICAL);
    $sizer->Add($self->{vsizer}, 1);

    return $self;
}

sub append_text {
    my $self = shift;
    my ($text) = @_;

    my $para = Wx::StaticText->new($self, -1, $text, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    $para->Wrap($self->{width});
    $para->SetMinSize([$self->{width}, -1]);
    $self->{vsizer}->Add($para, 0, wxALIGN_LEFT | wxTOP | wxBOTTOM, 10);
}

sub append_option {
    my $self = shift;
    my ($full_key) = @_;
    
    # populate repository with the factory default
    my ($opt_key, $opt_index) = split /#/, $full_key, 2;
    $self->config->apply(Slic3r::Config->new_from_defaults($opt_key));
    
    # draw the control
    my $optgroup = Slic3r::GUI::ConfigOptionsGroup->new(
        parent      => $self,
        title       => '',
        config      => $self->config,
        full_labels => 1,
    );
    $optgroup->append_single_option_line($opt_key, $opt_index);
    $self->{vsizer}->Add($optgroup->sizer, 0, wxEXPAND | wxTOP | wxBOTTOM, 10);
}

sub append_panel {
    my ($self, $panel) = @_;
    $self->{vsizer}->Add($panel, 0, wxEXPAND | wxTOP | wxBOTTOM, 10);
}

sub set_previous_page {
    my $self = shift;
    my ($previous_page) = @_;
    $self->{previous_page} = $previous_page;
}

sub GetPrev {
    my $self = shift;
    return $self->{previous_page};
}

sub set_next_page {
    my $self = shift;
    my ($next_page) = @_;
    $self->{next_page} = $next_page;
}

sub GetNext {
    my $self = shift;
    return $self->{next_page};
}

sub get_short_title {
    my $self = shift;
    return $self->{short_title};
}

sub build_index {
    my $self = shift;

    my $page = $self;
    $self->{index}->prepend_title($page->get_short_title) while ($page = $page->GetPrev);
    $page = $self;
    $self->{index}->append_title($page->get_short_title) while ($page = $page->GetNext);
}

sub config {
    my ($self) = @_;
    return $self->GetParent->{config};
}

package Slic3r::GUI::ConfigWizard::Page::Welcome;
use base 'Slic3r::GUI::ConfigWizard::Page';

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, "Welcome to the Slic3r Configuration $wizard", 'Welcome');

    $self->append_text('Hello, welcome to Slic3r! This '.lc($wizard).' helps you with the initial configuration; just a few settings and you will be ready to print.');
    $self->append_text('To import an existing configuration instead, cancel this '.lc($wizard).' and use the Open Config menu item found in the File menu.');
    $self->append_text('To continue, click Next.');

    return $self;
}

package Slic3r::GUI::ConfigWizard::Page::Firmware;
use base 'Slic3r::GUI::ConfigWizard::Page';

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, 'Firmware Type');

    $self->append_text('Choose the type of firmware used by your printer, then click Next.');
    $self->append_option('gcode_flavor');

    return $self;
}

package Slic3r::GUI::ConfigWizard::Page::Bed;
use base 'Slic3r::GUI::ConfigWizard::Page';

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, 'Bed Size');

    $self->append_text('Set the shape of your printer\'s bed, then click Next.');
    
    $self->config->apply(Slic3r::Config->new_from_defaults('bed_shape'));
    $self->{bed_shape_panel} = my $panel = Slic3r::GUI::BedShapePanel->new($self, $self->config->bed_shape);
    $self->{bed_shape_panel}->on_change(sub {
        $self->config->set('bed_shape', $self->{bed_shape_panel}->GetValue);
    });
    $self->append_panel($self->{bed_shape_panel});
    return $self;
}

package Slic3r::GUI::ConfigWizard::Page::Nozzle;
use base 'Slic3r::GUI::ConfigWizard::Page';

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, 'Nozzle Diameter');

    $self->append_text('Enter the diameter of your printer\'s hot end nozzle, then click Next.');
    $self->append_option('nozzle_diameter#0');

    return $self;
}

package Slic3r::GUI::ConfigWizard::Page::Filament;
use base 'Slic3r::GUI::ConfigWizard::Page';

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, 'Filament Diameter');

    $self->append_text('Enter the diameter of your filament, then click Next.');
    $self->append_text('Good precision is required, so use a caliper and do multiple measurements along the filament, then compute the average.');
    $self->append_option('filament_diameter#0');

    return $self;
}

package Slic3r::GUI::ConfigWizard::Page::Temperature;
use base 'Slic3r::GUI::ConfigWizard::Page';

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, 'Extrusion Temperature');

    $self->append_text('Enter the temperature needed for extruding your filament, then click Next.');
    $self->append_text('A rule of thumb is 160 to 230 °C for PLA, and 215 to 250 °C for ABS.');
    $self->append_option('temperature#0');

    return $self;
}

package Slic3r::GUI::ConfigWizard::Page::BedTemperature;
use base 'Slic3r::GUI::ConfigWizard::Page';

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, 'Bed Temperature');

    $self->append_text('Enter the bed temperature needed for getting your filament to stick to your heated bed, then click Next.');
    $self->append_text('A rule of thumb is 60 °C for PLA and 110 °C for ABS. Leave zero if you have no heated bed.');
    $self->append_option('bed_temperature');
    
    return $self;
}

package Slic3r::GUI::ConfigWizard::Page::Finished;
use base 'Slic3r::GUI::ConfigWizard::Page';

sub new {
    my $class = shift;
    my ($parent) = @_;
    my $self = $class->SUPER::new($parent, 'Congratulations!', 'Finish');

    $self->append_text("You have successfully completed the Slic3r Configuration $wizard. " .
                       'Slic3r is now configured for your printer and filament.');
    $self->append_text('To close this '.lc($wizard).' and apply the newly created configuration, click Finish.');

    return $self;
}

1;
