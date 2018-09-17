# Included in ObjectSettingsDialog -> ObjectPartsPanel.
# Maintains, displays, adds and removes overrides of slicing parameters for an object and its modifier mesh.

package Slic3r::GUI::Plater::OverrideSettingsPanel;
use strict;
use warnings;
use utf8;

use List::Util qw(first);
use Wx qw(:misc :sizer :button :combobox wxTAB_TRAVERSAL wxSUNKEN_BORDER wxBITMAP_TYPE_PNG wxTheApp);
use Wx::Event qw(EVT_BUTTON EVT_COMBOBOX EVT_LEFT_DOWN EVT_MENU);
use base 'Wx::ScrolledWindow';

use constant ICON_MATERIAL      => 0;
use constant ICON_SOLIDMESH     => 1;
use constant ICON_MODIFIERMESH  => 2;

use constant TYPE_OBJECT        => -1;
use constant TYPE_PART          => 0;
use constant TYPE_MODIFIER      => 1;
use constant TYPE_SUPPORT_ENFORCER => 2;
use constant TYPE_SUPPORT_BLOCKER => 3;

my %icons = (
    'Advanced'              => 'wand.png',
    'Extruders'             => 'funnel.png',
    'Extrusion Width'       => 'funnel.png',
    'Infill'                => 'infill.png',
    'Layers and Perimeters' => 'layers.png',
    'Skirt and brim'        => 'box.png',
    'Speed'                 => 'time.png',
    'Speed > Acceleration'  => 'time.png',
    'Support material'      => 'building.png',
);

sub new {
    my ($class, $parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    # C++ class Slic3r::DynamicPrintConfig, initially empty.
    $self->{default_config} = Slic3r::Config->new;
    $self->{config} = Slic3r::Config->new;
    # On change callback.
    $self->{on_change} = $params{on_change};
    $self->{type} = TYPE_OBJECT;
    $self->{fixed_options} = {};
    
    $self->{sizer} = Wx::BoxSizer->new(wxVERTICAL);
    
    $self->{options_sizer} = Wx::BoxSizer->new(wxVERTICAL);
    $self->{sizer}->Add($self->{options_sizer}, 0, wxEXPAND | wxALL, 0);

    # option selector
    {
        # create the button
        my $btn = $self->{btn_add} = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new(Slic3r::var("add.png"), wxBITMAP_TYPE_PNG),
            wxDefaultPosition, wxDefaultSize, Wx::wxBORDER_NONE);
        EVT_LEFT_DOWN($btn, sub {
            my $menu = Wx::Menu->new;
            # create category submenus
            my %categories = ();  # category => submenu
            foreach my $opt_key (@{$self->{options}}) {
                if (my $cat = $Slic3r::Config::Options->{$opt_key}{category}) {
                    $categories{$cat} //= Wx::Menu->new;
                }
            }
            # append submenus to main menu
            my @categories = ('Layers and Perimeters', 'Infill', 'Support material', 'Speed', 'Extruders', 'Extrusion Width', 'Advanced');
            #foreach my $cat (sort keys %categories) {
            foreach my $cat (@categories) {
                wxTheApp->append_submenu($menu, $cat, "", $categories{$cat}, undef, $icons{$cat});
            }
            # append options to submenus
            foreach my $opt_key (@{$self->{options}}) {
                my $cat = $Slic3r::Config::Options->{$opt_key}{category} or next;
                my $cb = sub {
                    $self->{config}->set($opt_key, $self->{default_config}->get($opt_key));
                    $self->update_optgroup;
                    $self->{on_change}->($opt_key) if $self->{on_change};
                };
                wxTheApp->append_menu_item($categories{$cat}, $self->{option_labels}{$opt_key},
                    $Slic3r::Config::Options->{$opt_key}{tooltip}, $cb);
            }
            $self->PopupMenu($menu, $btn->GetPosition);
            $menu->Destroy;
        });
        
        my $h_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        $h_sizer->Add($btn, 0, wxALL, 0);
        $self->{sizer}->Add($h_sizer, 0, wxEXPAND | wxBOTTOM, 10);
    }
    
    $self->SetSizer($self->{sizer});
    $self->SetScrollbars(0, 1, 0, 1);
    
    $self->set_opt_keys($params{opt_keys}) if $params{opt_keys};
    $self->update_optgroup;
    
    return $self;
}

sub set_default_config {
    my ($self, $config) = @_;
    $self->{default_config} = $config;
}

sub set_config {
    my ($self, $config) = @_;
    $self->{config} = $config;
    $self->update_optgroup;
}

sub set_opt_keys {
    my ($self, $opt_keys) = @_;
    # sort options by category+label
    $self->{option_labels} = { map { $_ => $Slic3r::Config::Options->{$_}{full_label} // $Slic3r::Config::Options->{$_}{label} } @$opt_keys };
    $self->{options} = [ sort { $self->{option_labels}{$a} cmp $self->{option_labels}{$b} } @$opt_keys ];
}

sub set_type {
    my ($self, $type) = @_;
    $self->{type} = $type;
    if ($type == TYPE_SUPPORT_ENFORCER || $type == TYPE_SUPPORT_BLOCKER) {
        $self->{btn_add}->Hide;
    } else {
        $self->{btn_add}->Show;
    }
}

sub set_fixed_options {
    my ($self, $opt_keys) = @_;
    $self->{fixed_options} = { map {$_ => 1} @$opt_keys };
    $self->update_optgroup;
}

sub update_optgroup {
    my $self = shift;
    
    $self->{options_sizer}->Clear(1);
    return if !defined $self->{config};

    if ($self->{type} != TYPE_OBJECT) {
        my $label = Wx::StaticText->new($self, -1, "Type:"),
        my $selection = [ "Part", "Modifier", "Support Enforcer", "Support Blocker" ];
        my $field = Wx::ComboBox->new($self, -1, $selection->[$self->{type}], wxDefaultPosition, Wx::Size->new(160, -1), $selection, wxCB_READONLY);
        my $sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        $sizer->Add($label, 1, wxEXPAND | wxALL, 5);
        $sizer->Add($field, 0, wxALL, 5);
        EVT_COMBOBOX($self, $field, sub {
            my $idx = $field->GetSelection;  # get index of selected value
            $self->{on_change}->("part_type", $idx) if $self->{on_change};
        });
        $self->{options_sizer}->Add($sizer, 0, wxEXPAND | wxBOTTOM, 0);
    }

    my %categories = ();
    if ($self->{type} != TYPE_SUPPORT_ENFORCER && $self->{type} != TYPE_SUPPORT_BLOCKER) {
        foreach my $opt_key (@{$self->{config}->get_keys}) {
            my $category = $Slic3r::Config::Options->{$opt_key}{category};
            $categories{$category} ||= [];
            push @{$categories{$category}}, $opt_key;
        }
    }
    foreach my $category (sort keys %categories) {
        my $optgroup = Slic3r::GUI::ConfigOptionsGroup->new(
            parent          => $self,
            title           => $category,
            config          => $self->{config},
            full_labels     => 1,
            label_font      => $Slic3r::GUI::small_font,
            sidetext_font   => $Slic3r::GUI::small_font,
            label_width     => 150,
            on_change       => sub { $self->{on_change}->() if $self->{on_change} },
            extra_column    => sub {
                my ($line) = @_;
                
                my $opt_key = $line->get_options->[0]->opt_id;  # we assume that we have one option per line
                
                # disallow deleting fixed options
                return undef if $self->{fixed_options}{$opt_key};
                
                my $btn = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new(Slic3r::var("delete.png"), wxBITMAP_TYPE_PNG),
                    wxDefaultPosition, wxDefaultSize, Wx::wxBORDER_NONE);
                EVT_BUTTON($self, $btn, sub {
                    $self->{config}->erase($opt_key);
                    $self->{on_change}->() if $self->{on_change};
                    wxTheApp->CallAfter(sub { $self->update_optgroup });
                });
                return $btn;
            },
        );
        foreach my $opt_key (sort @{$categories{$category}}) {
            $optgroup->append_single_option_line($opt_key);
        }
        $self->{options_sizer}->Add($optgroup->sizer, 0, wxEXPAND | wxBOTTOM, 0);
    }
    $self->GetParent->Layout;  # we need this for showing scrollbars
}

# work around a wxMAC bug causing controls not being disabled when calling Disable() on a Window
sub enable {
    my ($self) = @_;
    
    $self->{btn_add}->Enable;
    $self->Enable;
}

sub disable {
    my ($self) = @_;
    
    $self->{btn_add}->Disable;
    $self->Disable;
}

1;
