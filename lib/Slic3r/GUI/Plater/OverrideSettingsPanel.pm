package Slic3r::GUI::Plater::OverrideSettingsPanel;
use strict;
use warnings;
use utf8;

use List::Util qw(first);
use Wx qw(:misc :sizer :button wxTAB_TRAVERSAL wxSUNKEN_BORDER wxBITMAP_TYPE_PNG);
use Wx::Event qw(EVT_BUTTON EVT_LEFT_DOWN EVT_MENU);
use base 'Wx::ScrolledWindow';

use constant ICON_MATERIAL      => 0;
use constant ICON_SOLIDMESH     => 1;
use constant ICON_MODIFIERMESH  => 2;

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    $self->{config} = $params{config};  # may be passed as undef
    $self->{on_change} = $params{on_change};
    
    $self->{sizer} = Wx::BoxSizer->new(wxVERTICAL);
    
    $self->{options_sizer} = Wx::BoxSizer->new(wxVERTICAL);
    $self->{sizer}->Add($self->{options_sizer}, 0, wxEXPAND | wxALL, 0);
    
    # option selector
    {
        # create the button
        my $btn = $self->{btn_add} = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new("$Slic3r::var/add.png", wxBITMAP_TYPE_PNG),
            wxDefaultPosition, wxDefaultSize, Wx::wxBORDER_NONE);
        EVT_LEFT_DOWN($btn, sub {
            my $menu = Wx::Menu->new;
            foreach my $opt_key (@{$self->{options}}) {
                my $id = &Wx::NewId();
                $menu->Append($id, $self->{option_labels}{$opt_key});
                EVT_MENU($menu, $id, sub {
                    $self->{config}->apply(Slic3r::Config->new_from_defaults($opt_key));
                    $self->update_optgroup;
                    $self->{on_change}->() if $self->{on_change};
                });
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

sub set_opt_keys {
    my ($self, $opt_keys) = @_;
    
    # sort options by category+label
    $self->{option_labels} = {
        map { $_ => sprintf('%s > %s', $Slic3r::Config::Options->{$_}{category}, $Slic3r::Config::Options->{$_}{full_label} // $Slic3r::Config::Options->{$_}{label}) } @$opt_keys
    };
    $self->{options} = [ sort { $self->{option_labels}{$a} cmp $self->{option_labels}{$b} } @$opt_keys ];
}

sub set_config {
    my ($self, $config) = @_;
    $self->{config} = $config;
    $self->update_optgroup;
}

sub update_optgroup {
    my $self = shift;
    
    $self->{options_sizer}->Clear(1);
    return if !defined $self->{config};
    
    my %categories = ();
    foreach my $opt_key (@{$self->{config}->get_keys}) {
        my $category = $Slic3r::Config::Options->{$opt_key}{category};
        $categories{$category} ||= [];
        push @{$categories{$category}}, $opt_key;
    }
    foreach my $category (sort keys %categories) {
        my $optgroup = Slic3r::GUI::ConfigOptionsGroup->new(
            parent      => $self,
            title       => $category,
            config      => $self->{config},
            options     => [ sort @{$categories{$category}} ],
            full_labels => 1,
            label_font  => $Slic3r::GUI::small_font,
            sidetest_font => $Slic3r::GUI::small_font,
            label_width => 120,
            on_change   => sub { $self->{on_change}->() if $self->{on_change} },
            extra_column => sub {
                my ($line) = @_;
                my ($opt_key) = @{$line->{options}};  # we assume that we have one option per line
                
                # if this option is not listed in the ones the user can add, disallow deleting it
                return undef if !first { $_ eq $opt_key } @{$self->{options}};
                
                my $btn = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new("$Slic3r::var/delete.png", wxBITMAP_TYPE_PNG),
                    wxDefaultPosition, wxDefaultSize, Wx::wxBORDER_NONE);
                EVT_BUTTON($self, $btn, sub {
                    $self->{config}->erase($opt_key);
                    $self->{on_change}->() if $self->{on_change};
                    Slic3r::GUI->CallAfter(sub { $self->update_optgroup });
                });
                return $btn;
            },
        );
        $self->{options_sizer}->Add($optgroup->sizer, 0, wxEXPAND | wxBOTTOM, 0);
    }
    $self->Layout;
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
