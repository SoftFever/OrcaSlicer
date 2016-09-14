# The "Controller" tab to control the printer using serial / USB.
# This feature is rarely used. Much more often, the firmware reads the G-codes from a SD card.
# May there be multiple subtabs per each printer connected?

package Slic3r::GUI::Controller;
use strict;
use warnings;
use utf8;

use Wx qw(wxTheApp :frame :id :misc :sizer :bitmap :button :icon :dialog);
use Wx::Event qw(EVT_CLOSE EVT_LEFT_DOWN EVT_MENU);
use base qw(Wx::ScrolledWindow Class::Accessor);

__PACKAGE__->mk_accessors(qw(_selected_printer_preset));

our @ConfigOptions = qw(bed_shape serial_port serial_speed);

sub new {
    my ($class, $parent) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, [600,350]);
    
    $self->SetScrollbars(0, 1, 0, 1);
    $self->{sizer} = my $sizer = Wx::BoxSizer->new(wxVERTICAL);
    
    # warning to show when there are no printers configured
    {
        $self->{text_no_printers} = Wx::StaticText->new($self, -1,
            "No printers were configured for USB/serial control.",
            wxDefaultPosition, wxDefaultSize);
        $self->{sizer}->Add($self->{text_no_printers}, 0, wxTOP | wxLEFT, 30);
    }
    
    # button for adding new printer panels
    {
        my $btn = $self->{btn_add} = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new($Slic3r::var->("add.png"), wxBITMAP_TYPE_PNG),
            wxDefaultPosition, wxDefaultSize, Wx::wxBORDER_NONE);
        $btn->SetToolTipString("Add printer…")
            if $btn->can('SetToolTipString');
        
        EVT_LEFT_DOWN($btn, sub {
            my $menu = Wx::Menu->new;
            my %presets = wxTheApp->presets('printer');
            
            # remove printers that already exist
            my @panels = $self->print_panels;
            delete $presets{$_} for map $_->printer_name, @panels;
            
            foreach my $preset_name (sort keys %presets) {
                my $config = Slic3r::Config->load($presets{$preset_name});
                next if !$config->serial_port;
                
                my $id = &Wx::NewId();
                $menu->Append($id, $preset_name);
                EVT_MENU($menu, $id, sub {
                    $self->add_printer($preset_name, $config);
                });
            }
            $self->PopupMenu($menu, $btn->GetPosition);
            $menu->Destroy;
        });
        $self->{sizer}->Add($btn, 0, wxTOP | wxLEFT, 10);
    }
    
    $self->SetSizer($sizer);
    $self->SetMinSize($self->GetSize);
    #$sizer->SetSizeHints($self);
    
    EVT_CLOSE($self, sub {
        my (undef, $event) = @_;
        
        if ($event->CanVeto) {
            foreach my $panel ($self->print_panels) {
                if ($panel->printing) {
                    my $confirm = Wx::MessageDialog->new(
                        $self, "Printer '" . $panel->printer_name . "' is printing.\n\nDo you want to stop printing?",
                        'Unfinished Print', wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION,
                    );
                    if ($confirm->ShowModal == wxID_NO) {
                        $event->Veto;
                        return;
                    }
                }
            }
        }
        foreach my $panel ($self->print_panels) {
            $panel->disconnect;
        }
        
        $event->Skip;
    });
    
    $self->Layout;
    
    return $self;
}

sub OnActivate {
    my ($self) = @_;
    
    # get all available presets
    my %presets = ();
    {
        my %all = wxTheApp->presets('printer');
        my %configs = map { my $name = $_; $name => Slic3r::Config->load($all{$name}) } keys %all;
        %presets = map { $_ => $configs{$_} } grep $configs{$_}->serial_port, keys %all;
    }
    
    # decide which ones we want to keep
    my %active = ();
    
    # keep the ones that are currently connected or have jobs in queue
    $active{$_} = 1 for map $_->printer_name,
        grep { $_->is_connected || @{$_->jobs} > 0 }
        $self->print_panels;
    
    if (%presets) {
        # if there are no active panels, use sensible defaults
        if (!%active && keys %presets <= 2) {
            # if only one or two presets exist, load them
            $active{$_} = 1 for keys %presets;
        }
        if (!%active) {
            # enable printers whose port is available
            my %ports = map { $_ => 1 } wxTheApp->scan_serial_ports;
            $active{$_} = 1
                for grep exists $ports{$presets{$_}->serial_port}, keys %presets;
        }
        if (!%active && $self->_selected_printer_preset) {
            # enable currently selected printer if it is configured
            $active{$self->_selected_printer_preset} = 1
                if $presets{$self->_selected_printer_preset};
        }
    }
    
    # apply changes
    for my $panel ($self->print_panels) {
        next if $active{$panel->printer_name};
        
        $self->{sizer}->DetachWindow($panel);
        $panel->Destroy;
    }
    $self->add_printer($_, $presets{$_}) for sort keys %active;
    
    # show/hide the warning about no printers
    $self->{text_no_printers}->Show(!%presets);
    
    # show/hide the Add button
    $self->{btn_add}->Show(keys %presets != keys %active);
    
    $self->Layout;
    
    # we need this in order to trigger the OnSize event of wxScrolledWindow which
    # recalculates the virtual size
    Wx::GetTopLevelParent($self)->SendSizeEvent;
}

sub add_printer {
    my ($self, $printer_name, $config) = @_;
    
    # check that printer doesn't exist already
    foreach my $panel ($self->print_panels) {
        if ($panel->printer_name eq $printer_name) {
            return $panel;
        }
    }
    
    my $printer_panel = Slic3r::GUI::Controller::PrinterPanel->new($self, $printer_name, $config);
    $self->{sizer}->Prepend($printer_panel, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 10);
    $self->Layout;
    
    return $printer_panel;
}

sub print_panels {
    my ($self) = @_;
    return grep $_->isa('Slic3r::GUI::Controller::PrinterPanel'),
        map $_->GetWindow, $self->{sizer}->GetChildren;
}

sub update_presets {
    my $self = shift;
    my ($group, $presets, $selected, $is_dirty) = @_;
    
    # update configs of currently loaded print panels
    foreach my $panel ($self->print_panels) {
        foreach my $preset (@$presets) {
            if ($panel->printer_name eq $preset->name) {
                my $config = $preset->config(\@ConfigOptions);
                $panel->config->apply($config);
            }
        }
    }
    
    $self->_selected_printer_preset($presets->[$selected]->name);
}

1;
