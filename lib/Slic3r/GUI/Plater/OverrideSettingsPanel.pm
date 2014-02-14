package Slic3r::GUI::Plater::OverrideSettingsPanel;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename);
use Wx qw(:misc :sizer :button wxTAB_TRAVERSAL wxSUNKEN_BORDER wxBITMAP_TYPE_PNG);
use Wx::Event qw(EVT_BUTTON);
use base 'Wx::ScrolledWindow';

use constant ICON_MATERIAL      => 0;
use constant ICON_SOLIDMESH     => 1;
use constant ICON_MODIFIERMESH  => 2;

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    $self->{config} = $params{config};  # may be passed as undef
    my @opt_keys = @{$params{opt_keys}};
    
    $self->{sizer} = Wx::BoxSizer->new(wxVERTICAL);
    
    # option selector
    {
        # get all options with object scope and sort them by category+label
        my %settings = map { $_ => sprintf('%s > %s', $Slic3r::Config::Options->{$_}{category}, $Slic3r::Config::Options->{$_}{full_label} // $Slic3r::Config::Options->{$_}{label}) } @opt_keys;
        $self->{options} = [ sort { $settings{$a} cmp $settings{$b} } keys %settings ];
        my $choice = Wx::Choice->new($self, -1, wxDefaultPosition, [150, -1], [ map $settings{$_}, @{$self->{options}} ]);
        
        # create the button
        my $btn = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new("$Slic3r::var/add.png", wxBITMAP_TYPE_PNG));
        EVT_BUTTON($self, $btn, sub {
            my $idx = $choice->GetSelection;
            return if $idx == -1;  # lack of selected item, can happen on Windows
            my $opt_key = $self->{options}[$idx];
            $self->{config}->apply(Slic3r::Config->new_from_defaults($opt_key));
            $self->update_optgroup;
        });
        
        my $h_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
        $h_sizer->Add($choice, 1, wxEXPAND | wxALL, 0);
        $h_sizer->Add($btn, 0, wxEXPAND | wxLEFT, 10);
        $self->{sizer}->Add($h_sizer, 0, wxEXPAND | wxBOTTOM, 10);
    }
    
    $self->{options_sizer} = Wx::BoxSizer->new(wxVERTICAL);
    $self->{sizer}->Add($self->{options_sizer}, 0, wxEXPAND | wxALL, 0);
    
    $self->SetSizer($self->{sizer});
    $self->SetScrollbars(0, 1, 0, 1);
    
    $self->update_optgroup;
    
    return $self;
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
            extra_column => sub {
                my ($line) = @_;
                my ($opt_key) = @{$line->{options}};  # we assume that we have one option per line
                my $btn = Wx::BitmapButton->new($self, -1, Wx::Bitmap->new("$Slic3r::var/delete.png", wxBITMAP_TYPE_PNG));
                EVT_BUTTON($self, $btn, sub {
                    $self->{config}->erase($opt_key);
                    Slic3r::GUI->CallAfter(sub { $self->update_optgroup });
                });
                return $btn;
            },
        );
        $self->{options_sizer}->Add($optgroup->sizer, 0, wxEXPAND | wxBOTTOM, 10);
    }
    $self->Layout;
}

1;
