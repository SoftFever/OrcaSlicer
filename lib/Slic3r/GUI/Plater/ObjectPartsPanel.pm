package Slic3r::GUI::Plater::ObjectPartsPanel;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename);
use Wx qw(:misc :sizer :treectrl :button wxTAB_TRAVERSAL wxSUNKEN_BORDER wxBITMAP_TYPE_PNG
    wxTheApp);
use Wx::Event qw(EVT_BUTTON EVT_TREE_ITEM_COLLAPSING EVT_TREE_SEL_CHANGED);
use base 'Wx::Panel';

use constant ICON_OBJECT        => 0;
use constant ICON_SOLIDMESH     => 1;
use constant ICON_MODIFIERMESH  => 2;

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    
    my $object = $self->{model_object} = $params{model_object};
    
    # create TreeCtrl
    my $tree = $self->{tree} = Wx::TreeCtrl->new($self, -1, wxDefaultPosition, [300, 100], 
        wxTR_NO_BUTTONS | wxSUNKEN_BORDER | wxTR_HAS_VARIABLE_ROW_HEIGHT
        | wxTR_SINGLE | wxTR_NO_BUTTONS);
    {
        $self->{tree_icons} = Wx::ImageList->new(16, 16, 1);
        $tree->AssignImageList($self->{tree_icons});
        $self->{tree_icons}->Add(Wx::Bitmap->new("$Slic3r::var/brick.png", wxBITMAP_TYPE_PNG));     # ICON_OBJECT
        $self->{tree_icons}->Add(Wx::Bitmap->new("$Slic3r::var/package.png", wxBITMAP_TYPE_PNG));   # ICON_SOLIDMESH
        $self->{tree_icons}->Add(Wx::Bitmap->new("$Slic3r::var/plugin.png", wxBITMAP_TYPE_PNG));    # ICON_MODIFIERMESH
        
        my $rootId = $tree->AddRoot("Object", ICON_OBJECT);
        $tree->SetPlData($rootId, { type => 'object' });
    }
    
    # buttons
    $self->{btn_load_part} = Wx::Button->new($self, -1, "Load part…", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
    $self->{btn_load_modifier} = Wx::Button->new($self, -1, "Load modifier…", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
    $self->{btn_delete} = Wx::Button->new($self, -1, "Delete part", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
    if ($Slic3r::GUI::have_button_icons) {
        $self->{btn_load_part}->SetBitmap(Wx::Bitmap->new("$Slic3r::var/brick_add.png", wxBITMAP_TYPE_PNG));
        $self->{btn_load_modifier}->SetBitmap(Wx::Bitmap->new("$Slic3r::var/brick_add.png", wxBITMAP_TYPE_PNG));
        $self->{btn_delete}->SetBitmap(Wx::Bitmap->new("$Slic3r::var/brick_delete.png", wxBITMAP_TYPE_PNG));
    }
    
    # buttons sizer
    my $buttons_sizer = Wx::BoxSizer->new(wxHORIZONTAL);
    $buttons_sizer->Add($self->{btn_load_part}, 0);
    $buttons_sizer->Add($self->{btn_load_modifier}, 0);
    $buttons_sizer->Add($self->{btn_delete}, 0);
    $self->{btn_load_part}->SetFont($Slic3r::GUI::small_font);
    $self->{btn_load_modifier}->SetFont($Slic3r::GUI::small_font);
    $self->{btn_delete}->SetFont($Slic3r::GUI::small_font);
    
    # part settings panel
    $self->{settings_panel} = Slic3r::GUI::Plater::OverrideSettingsPanel->new($self, on_change => sub { $self->{part_settings_changed} = 1; });
    my $settings_sizer = Wx::StaticBoxSizer->new($self->{staticbox} = Wx::StaticBox->new($self, -1, "Part Settings"), wxVERTICAL);
    $settings_sizer->Add($self->{settings_panel}, 1, wxEXPAND | wxALL, 0);
    
    # left pane with tree
    my $left_sizer = Wx::BoxSizer->new(wxVERTICAL);
    $left_sizer->Add($tree, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    $left_sizer->Add($buttons_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    $left_sizer->Add($settings_sizer, 1, wxEXPAND | wxALL, 0);
    
    # right pane with preview canvas
    my $canvas;
    if ($Slic3r::GUI::have_OpenGL) {
        $canvas = $self->{canvas} = Slic3r::GUI::3DScene->new($self);
        $canvas->enable_picking(1);
        $canvas->select_by('volume');
        
        $canvas->on_select(sub {
            my ($volume_idx) = @_;
            
            # convert scene volume to model object volume
            $self->reload_tree($canvas->volume_idx($volume_idx));
        });
        
        $canvas->load_object($self->{model_object}, undef, [0]);
        $canvas->set_auto_bed_shape;
        $canvas->SetSize([500,500]);
        $canvas->zoom_to_volumes;
    }
    
    $self->{sizer} = Wx::BoxSizer->new(wxHORIZONTAL);
    $self->{sizer}->Add($left_sizer, 0, wxEXPAND | wxALL, 0);
    $self->{sizer}->Add($canvas, 1, wxEXPAND | wxALL, 0) if $canvas;
    
    $self->SetSizer($self->{sizer});
    $self->{sizer}->SetSizeHints($self);
    
    # attach events
    EVT_TREE_ITEM_COLLAPSING($self, $tree, sub {
        my ($self, $event) = @_;
        $event->Veto;
    });
    EVT_TREE_SEL_CHANGED($self, $tree, sub {
        my ($self, $event) = @_;
        return if $self->{disable_tree_sel_changed_event};
        $self->selection_changed;
    });
    EVT_BUTTON($self, $self->{btn_load_part}, sub { $self->on_btn_load(0) });
    EVT_BUTTON($self, $self->{btn_load_modifier}, sub { $self->on_btn_load(1) });
    EVT_BUTTON($self, $self->{btn_delete}, \&on_btn_delete);
    
    $self->reload_tree;
    
    return $self;
}

sub reload_tree {
    my ($self, $selected_volume_idx) = @_;
    
    $selected_volume_idx //= -1;
    my $object  = $self->{model_object};
    my $tree    = $self->{tree};
    my $rootId  = $tree->GetRootItem;
    
    # despite wxWidgets states that DeleteChildren "will not generate any events unlike Delete() method",
    # the MSW implementation of DeleteChildren actually calls Delete() for each item, so
    # EVT_TREE_SEL_CHANGED is being called, with bad effects (the event handler is called; this 
    # subroutine is never continued; an invisible EndModal is called on the dialog causing Plater
    # to continue its logic and rescheduling the background process etc. GH #2774)
    $self->{disable_tree_sel_changed_event} = 1;
    $tree->DeleteChildren($rootId);
    $self->{disable_tree_sel_changed_event} = 0;
    
    my $selectedId = $rootId;
    foreach my $volume_id (0..$#{$object->volumes}) {
        my $volume = $object->volumes->[$volume_id];
        
        my $icon = $volume->modifier ? ICON_MODIFIERMESH : ICON_SOLIDMESH;
        my $itemId = $tree->AppendItem($rootId, $volume->name || $volume_id, $icon);
        if ($volume_id == $selected_volume_idx) {
            $selectedId = $itemId;
        }
        $tree->SetPlData($itemId, {
            type        => 'volume',
            volume_id   => $volume_id,
        });
    }
    $tree->ExpandAll;
    
    Slic3r::GUI->CallAfter(sub {
        $self->{tree}->SelectItem($selectedId);
        
        # SelectItem() should trigger EVT_TREE_SEL_CHANGED as per wxWidgets docs,
        # but in fact it doesn't if the given item is already selected (this happens
        # on first load)
        $self->selection_changed;
    });
}

sub get_selection {
    my ($self) = @_;
    
    my $nodeId = $self->{tree}->GetSelection;
    if ($nodeId->IsOk) {
        return $self->{tree}->GetPlData($nodeId);
    }
    return undef;
}

sub selection_changed {
    my ($self) = @_;
    
    # deselect all meshes
    if ($self->{canvas}) {
        $_->selected(0) for @{$self->{canvas}->volumes};
    }
    
    # disable things as if nothing is selected
    $self->{btn_delete}->Disable;
    $self->{settings_panel}->disable;
    $self->{settings_panel}->set_config(undef);
    
    if (my $itemData = $self->get_selection) {
        my ($config, @opt_keys);
        if ($itemData->{type} eq 'volume') {
            # select volume in 3D preview
            if ($self->{canvas}) {
                $self->{canvas}->volumes->[ $itemData->{volume_id} ]{selected} = 1;
            }
            $self->{btn_delete}->Enable;
            
            # attach volume config to settings panel
            my $volume = $self->{model_object}->volumes->[ $itemData->{volume_id} ];
            $config = $volume->config;
            $self->{staticbox}->SetLabel('Part Settings');
            
            # get default values
            @opt_keys = @{Slic3r::Config::PrintRegion->new->get_keys};
        } elsif ($itemData->{type} eq 'object') {
            # select nothing in 3D preview
            
            # attach object config to settings panel
            $self->{staticbox}->SetLabel('Object Settings');
            @opt_keys = (map @{$_->get_keys}, Slic3r::Config::PrintObject->new, Slic3r::Config::PrintRegion->new);
            $config = $self->{model_object}->config;
        }
        # get default values
        my $default_config = Slic3r::Config->new_from_defaults(@opt_keys);
        
        # append default extruder
        push @opt_keys, 'extruder';
        $default_config->set('extruder', 0);
        $config->set_ifndef('extruder', 0);
        $self->{settings_panel}->set_default_config($default_config);
        $self->{settings_panel}->set_config($config);
        $self->{settings_panel}->set_opt_keys(\@opt_keys);
        $self->{settings_panel}->set_fixed_options([qw(extruder)]);
        $self->{settings_panel}->enable;
    }
    
    $self->{canvas}->Render if $self->{canvas};
}

sub on_btn_load {
    my ($self, $is_modifier) = @_;
    
    my @input_files = wxTheApp->open_model($self);
    foreach my $input_file (@input_files) {
        my $model = eval { Slic3r::Model->read_from_file($input_file) };
        if ($@) {
            Slic3r::GUI::show_error($self, $@);
            next;
        }
        
        foreach my $object (@{$model->objects}) {
            foreach my $volume (@{$object->volumes}) {
                my $new_volume = $self->{model_object}->add_volume($volume);
                $new_volume->set_modifier($is_modifier);
                $new_volume->set_name(basename($input_file));
                
                # apply the same translation we applied to the object
                $new_volume->mesh->translate(@{$self->{model_object}->origin_translation});
                
                # set a default extruder value, since user can't add it manually
                $new_volume->config->set_ifndef('extruder', 0);
                
                $self->{parts_changed} = 1;
            }
        }
    }
    
    $self->_parts_changed;
}

sub on_btn_delete {
    my ($self) = @_;
    
    my $itemData = $self->get_selection;
    if ($itemData && $itemData->{type} eq 'volume') {
        my $volume = $self->{model_object}->volumes->[$itemData->{volume_id}];
        
        # if user is deleting the last solid part, throw error
        if (!$volume->modifier && scalar(grep !$_->modifier, @{$self->{model_object}->volumes}) == 1) {
            Slic3r::GUI::show_error($self, "You can't delete the last solid part from this object.");
            return;
        }
        
        $self->{model_object}->delete_volume($itemData->{volume_id});
        $self->{parts_changed} = 1;
    }
    
    $self->_parts_changed;
}

sub _parts_changed {
    my ($self) = @_;
    
    $self->reload_tree;
    if ($self->{canvas}) {
        $self->{canvas}->reset_objects;
        $self->{canvas}->load_object($self->{model_object});
        $self->{canvas}->zoom_to_volumes;
        $self->{canvas}->Render;
    }
}

sub CanClose {
    my $self = shift;
    
    return 1;  # skip validation for now
    
    # validate options before allowing user to dismiss the dialog
    # the validate method only works on full configs so we have
    # to merge our settings with the default ones
    my $config = Slic3r::Config->merge($self->GetParent->GetParent->GetParent->GetParent->GetParent->config, $self->model_object->config);
    eval {
        $config->validate;
    };
    return 0 if Slic3r::GUI::catch_error($self);    
    return 1;
}

sub PartsChanged {
    my ($self) = @_;
    return $self->{parts_changed};
}

sub PartSettingsChanged {
    my ($self) = @_;
    return $self->{part_settings_changed};
}

1;
