# Configuration of mesh modifiers and their parameters.
# This panel is inserted into ObjectSettingsDialog.

package Slic3r::GUI::Plater::ObjectPartsPanel;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename);
use Wx qw(:misc :sizer :treectrl :button :keycode wxTAB_TRAVERSAL wxSUNKEN_BORDER wxBITMAP_TYPE_PNG wxID_CANCEL wxMOD_CONTROL
    wxTheApp);
use Wx::Event qw(EVT_BUTTON EVT_TREE_ITEM_COLLAPSING EVT_TREE_SEL_CHANGED EVT_TREE_KEY_DOWN EVT_KEY_DOWN);
use List::Util qw(max);
use base 'Wx::Panel';

use constant ICON_OBJECT        => 0;
use constant ICON_SOLIDMESH     => 1;
use constant ICON_MODIFIERMESH  => 2;

sub new {
    my ($class, $parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    
    # C++ type Slic3r::ModelObject
    my $object = $self->{model_object} = $params{model_object};

    # Save state for sliders.
    $self->{move_options} = {
        x               => 0,
        y               => 0,
        z               => 0,
    };
    $self->{last_coords} = {
        x               => 0,
        y               => 0,
        z               => 0,
    };
    
    # create TreeCtrl
    my $tree = $self->{tree} = Wx::TreeCtrl->new($self, -1, wxDefaultPosition, [300, 100], 
        wxTR_NO_BUTTONS | wxSUNKEN_BORDER | wxTR_HAS_VARIABLE_ROW_HEIGHT
        | wxTR_SINGLE);
    {
        $self->{tree_icons} = Wx::ImageList->new(16, 16, 1);
        $tree->AssignImageList($self->{tree_icons});
        $self->{tree_icons}->Add(Wx::Bitmap->new(Slic3r::var("brick.png"), wxBITMAP_TYPE_PNG));     # ICON_OBJECT
        $self->{tree_icons}->Add(Wx::Bitmap->new(Slic3r::var("package.png"), wxBITMAP_TYPE_PNG));   # ICON_SOLIDMESH
        $self->{tree_icons}->Add(Wx::Bitmap->new(Slic3r::var("plugin.png"), wxBITMAP_TYPE_PNG));    # ICON_MODIFIERMESH
        
        my $rootId = $tree->AddRoot("Object", ICON_OBJECT);
        $tree->SetPlData($rootId, { type => 'object' });
    }
    
    # buttons
    $self->{btn_load_part} = Wx::Button->new($self, -1, "Load part…", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
    $self->{btn_load_modifier} = Wx::Button->new($self, -1, "Load modifier…", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
    $self->{btn_load_lambda_modifier} = Wx::Button->new($self, -1, "Load generic…", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
    $self->{btn_delete} = Wx::Button->new($self, -1, "Delete part", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
    $self->{btn_split} = Wx::Button->new($self, -1, "Split part", wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
    $self->{btn_move_up} = Wx::Button->new($self, -1, "", wxDefaultPosition, [40, -1], wxBU_LEFT);
    $self->{btn_move_down} = Wx::Button->new($self, -1, "", wxDefaultPosition, [40, -1], wxBU_LEFT);
    $self->{btn_load_part}->SetBitmap(Wx::Bitmap->new(Slic3r::var("brick_add.png"), wxBITMAP_TYPE_PNG));
    $self->{btn_load_modifier}->SetBitmap(Wx::Bitmap->new(Slic3r::var("brick_add.png"), wxBITMAP_TYPE_PNG));
    $self->{btn_load_lambda_modifier}->SetBitmap(Wx::Bitmap->new(Slic3r::var("brick_add.png"), wxBITMAP_TYPE_PNG));
    $self->{btn_delete}->SetBitmap(Wx::Bitmap->new(Slic3r::var("brick_delete.png"), wxBITMAP_TYPE_PNG));
    $self->{btn_split}->SetBitmap(Wx::Bitmap->new(Slic3r::var("shape_ungroup.png"), wxBITMAP_TYPE_PNG));
    $self->{btn_move_up}->SetBitmap(Wx::Bitmap->new(Slic3r::var("bullet_arrow_up.png"), wxBITMAP_TYPE_PNG));
    $self->{btn_move_down}->SetBitmap(Wx::Bitmap->new(Slic3r::var("bullet_arrow_down.png"), wxBITMAP_TYPE_PNG));
    
    # buttons sizer
    my $buttons_sizer = Wx::GridSizer->new(2, 3);
    $buttons_sizer->Add($self->{btn_load_part}, 0, wxEXPAND | wxBOTTOM | wxRIGHT, 5);
    $buttons_sizer->Add($self->{btn_load_modifier}, 0, wxEXPAND | wxBOTTOM | wxRIGHT, 5);
    $buttons_sizer->Add($self->{btn_load_lambda_modifier}, 0, wxEXPAND | wxBOTTOM, 5);
    $buttons_sizer->Add($self->{btn_delete}, 0, wxEXPAND | wxRIGHT, 5);
    $buttons_sizer->Add($self->{btn_split}, 0, wxEXPAND | wxRIGHT, 5);
    {
        my $up_down_sizer = Wx::GridSizer->new(1, 2);
        $up_down_sizer->Add($self->{btn_move_up}, 0, wxEXPAND | wxRIGHT, 5);
        $up_down_sizer->Add($self->{btn_move_down}, 0, wxEXPAND, 5);
        $buttons_sizer->Add($up_down_sizer, 0, wxEXPAND, 5);
    }
    $self->{btn_load_part}->SetFont($Slic3r::GUI::small_font);
    $self->{btn_load_modifier}->SetFont($Slic3r::GUI::small_font);
    $self->{btn_load_lambda_modifier}->SetFont($Slic3r::GUI::small_font);
    $self->{btn_delete}->SetFont($Slic3r::GUI::small_font);
    $self->{btn_split}->SetFont($Slic3r::GUI::small_font);
    $self->{btn_move_up}->SetFont($Slic3r::GUI::small_font);
    $self->{btn_move_down}->SetFont($Slic3r::GUI::small_font);
    
    # part settings panel
    $self->{settings_panel} = Slic3r::GUI::Plater::OverrideSettingsPanel->new($self, on_change => sub { $self->{part_settings_changed} = 1; $self->_update_canvas; });
    my $settings_sizer = Wx::StaticBoxSizer->new($self->{staticbox} = Wx::StaticBox->new($self, -1, "Part Settings"), wxVERTICAL);
    $settings_sizer->Add($self->{settings_panel}, 1, wxEXPAND | wxALL, 0);

    my $optgroup_movers;
    $optgroup_movers = $self->{optgroup_movers} = Slic3r::GUI::OptionsGroup->new(
        parent      => $self,
        title       => 'Move',
        on_change   => sub {
            my ($opt_id) = @_;
            # There seems to be an issue with wxWidgets 3.0.2/3.0.3, where the slider
            # genates tens of events for a single value change.
            # Only trigger the recalculation if the value changes
            # or a live preview was activated and the mesh cut is not valid yet.
            if ($self->{move_options}{$opt_id} != $optgroup_movers->get_value($opt_id)) {
                $self->{move_options}{$opt_id} = $optgroup_movers->get_value($opt_id);
                wxTheApp->CallAfter(sub {
                    $self->_update;
                });
            }
        },
        label_width  => 20,
    );
    $optgroup_movers->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id      => 'x',
        type        => 'slider',
        label       => 'X',
        default     => 0,
        min         => -($self->{model_object}->bounding_box->size->x)*4,
        max         => $self->{model_object}->bounding_box->size->x*4,
        full_width  => 1,
    ));
    $optgroup_movers->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id      => 'y',
        type        => 'slider',
        label       => 'Y',
        default     => 0,
        min         => -($self->{model_object}->bounding_box->size->y)*4,
        max         => $self->{model_object}->bounding_box->size->y*4,
        full_width  => 1,
    ));
    $optgroup_movers->append_single_option_line(Slic3r::GUI::OptionsGroup::Option->new(
        opt_id      => 'z',
        type        => 'slider',
        label       => 'Z',
        default     => 0,
        min         => -($self->{model_object}->bounding_box->size->z)*4,
        max         => $self->{model_object}->bounding_box->size->z*4,
        full_width  => 1,
    ));
 
    # left pane with tree
    my $left_sizer = Wx::BoxSizer->new(wxVERTICAL);
    $left_sizer->Add($tree, 3, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    $left_sizer->Add($buttons_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    $left_sizer->Add($settings_sizer, 5, wxEXPAND | wxALL, 0);
    $left_sizer->Add($optgroup_movers->sizer, 0, wxEXPAND | wxBOTTOM | wxLEFT | wxRIGHT, 10);
    
    # right pane with preview canvas
    my $canvas;
    if ($Slic3r::GUI::have_OpenGL) {
        $canvas = $self->{canvas} = Slic3r::GUI::3DScene->new($self);
        Slic3r::GUI::_3DScene::enable_picking($canvas, 1);
        Slic3r::GUI::_3DScene::set_select_by($canvas, 'volume');
        Slic3r::GUI::_3DScene::register_on_select_object_callback($canvas, sub {
            my ($volume_idx) = @_;
            $self->reload_tree($volume_idx);
        });
        Slic3r::GUI::_3DScene::load_model_object($canvas, $self->{model_object}, 0, [0]);
        Slic3r::GUI::_3DScene::set_auto_bed_shape($canvas);
        Slic3r::GUI::_3DScene::set_axes_length($canvas, 2.0 * max(@{ Slic3r::GUI::_3DScene::get_volumes_bounding_box($canvas)->size }));
        $canvas->SetSize([500,700]);
        Slic3r::GUI::_3DScene::set_config($canvas, $self->GetParent->GetParent->GetParent->{config});
        Slic3r::GUI::_3DScene::update_volumes_colors_by_extruder($canvas);
        Slic3r::GUI::_3DScene::enable_force_zoom_to_bed($canvas, 1);
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
    EVT_TREE_KEY_DOWN($self, $tree, \&on_tree_key_down);
    EVT_BUTTON($self, $self->{btn_load_part}, sub { $self->on_btn_load(0) });
    EVT_BUTTON($self, $self->{btn_load_modifier}, sub { $self->on_btn_load(1) });
    EVT_BUTTON($self, $self->{btn_load_lambda_modifier}, sub { $self->on_btn_lambda(1) });
    EVT_BUTTON($self, $self->{btn_delete}, \&on_btn_delete);
    EVT_BUTTON($self, $self->{btn_split}, \&on_btn_split);
    EVT_BUTTON($self, $self->{btn_move_up}, \&on_btn_move_up);
    EVT_BUTTON($self, $self->{btn_move_down}, \&on_btn_move_down);
    EVT_KEY_DOWN($canvas, sub {
        my ($canvas, $event) = @_;
        if ($event->GetKeyCode == WXK_DELETE) {
            $canvas->GetParent->on_btn_delete;
        } else {
            $event->Skip;
        }
    });
    
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
        Slic3r::GUI::_3DScene::deselect_volumes($self->{canvas});
    }
    
    # disable things as if nothing is selected
    $self->{'btn_' . $_}->Disable for (qw(delete move_up move_down split));
    $self->{settings_panel}->disable;
    $self->{settings_panel}->set_config(undef);

    # reset move sliders
    $self->{optgroup_movers}->set_value("x", 0);
    $self->{optgroup_movers}->set_value("y", 0);
    $self->{optgroup_movers}->set_value("z", 0);
    $self->{move_options} = {
        x               => 0,
        y               => 0,
        z               => 0,
    };
    $self->{last_coords} = {
        x               => 0,
        y               => 0,
        z               => 0,
    };
    
    if (my $itemData = $self->get_selection) {
        my ($config, @opt_keys);
        if ($itemData->{type} eq 'volume') {
            # select volume in 3D preview
            if ($self->{canvas}) {
                Slic3r::GUI::_3DScene::select_volume($self->{canvas}, $itemData->{volume_id});
            }
            $self->{btn_delete}->Enable;
            $self->{btn_split}->Enable;
            $self->{btn_move_up}->Enable if $itemData->{volume_id} > 0;
            $self->{btn_move_down}->Enable if $itemData->{volume_id} + 1 < $self->{model_object}->volumes_count;
            
            # attach volume config to settings panel
            my $volume = $self->{model_object}->volumes->[ $itemData->{volume_id} ];
   
            if ($volume->modifier) {
                $self->{optgroup_movers}->enable;
            } else {
                $self->{optgroup_movers}->disable;
            }
            $config = $volume->config;
            $self->{staticbox}->SetLabel('Part Settings');
            
            # get default values
            @opt_keys = @{Slic3r::Config::PrintRegion->new->get_keys};
        } elsif ($itemData->{type} eq 'object') {
            # select nothing in 3D preview
            
            # attach object config to settings panel
            $self->{optgroup_movers}->disable;
            $self->{staticbox}->SetLabel('Object Settings');
            @opt_keys = (map @{$_->get_keys}, Slic3r::Config::PrintObject->new, Slic3r::Config::PrintRegion->new);
            $config = $self->{model_object}->config;
        }
        # get default values
        my $default_config = Slic3r::Config::new_from_defaults_keys(\@opt_keys);
        
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
    
    Slic3r::GUI::_3DScene::render($self->{canvas}) if $self->{canvas};
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

sub on_btn_lambda {
    my ($self, $is_modifier) = @_;
    
    my $dlg = Slic3r::GUI::Plater::LambdaObjectDialog->new($self);
    if ($dlg->ShowModal() == wxID_CANCEL) {
        return;
    }
    my $params = $dlg->ObjectParameter;
    my $type = "".$params->{"type"};
    my $name = "lambda-".$params->{"type"};
    my $mesh;

    if ($type eq "box") {
        $mesh = Slic3r::TriangleMesh::cube($params->{"dim"}[0], $params->{"dim"}[1], $params->{"dim"}[2]);
    } elsif ($type eq "cylinder") {
        $mesh = Slic3r::TriangleMesh::cylinder($params->{"cyl_r"}, $params->{"cyl_h"});
    } elsif ($type eq "sphere") {
        $mesh = Slic3r::TriangleMesh::sphere($params->{"sph_rho"});
    } elsif ($type eq "slab") {
        $mesh = Slic3r::TriangleMesh::cube($self->{model_object}->bounding_box->size->x*1.5, $self->{model_object}->bounding_box->size->y*1.5, $params->{"slab_h"});
        # box sets the base coordinate at 0,0, move to center of plate and move it up to initial_z
        $mesh->translate(-$self->{model_object}->bounding_box->size->x*1.5/2.0, -$self->{model_object}->bounding_box->size->y*1.5/2.0, $params->{"slab_z"});
    } else {
        return;
    }
    $mesh->repair;
    my $new_volume = $self->{model_object}->add_volume(mesh => $mesh);
    $new_volume->set_modifier($is_modifier);
    $new_volume->set_name($name);

    # set a default extruder value, since user can't add it manually
    $new_volume->config->set_ifndef('extruder', 0);

    $self->{parts_changed} = 1;
    $self->_parts_changed;
}

sub on_tree_key_down {
    my ($self, $event) = @_;
    my $keycode = $event->GetKeyCode;    
    # Wx >= 0.9911
    if (defined(&Wx::TreeEvent::GetKeyEvent)) { 
        if ($event->GetKeyEvent->GetModifiers & wxMOD_CONTROL) {
            if ($keycode == WXK_UP) {
                $event->Skip;
                $self->on_btn_move_up;
            } elsif ($keycode == WXK_DOWN) {
                $event->Skip;
                $self->on_btn_move_down;
            }
        } elsif ($keycode == WXK_DELETE) {
            $self->on_btn_delete;
        }
    }
}

sub on_btn_move_up {
    my ($self) = @_;
    my $itemData = $self->get_selection;
    if ($itemData && $itemData->{type} eq 'volume') {
        my $volume_id = $itemData->{volume_id};
        if ($self->{model_object}->move_volume_up($volume_id)) {
            Slic3r::GUI::_3DScene::move_volume_up($self->{canvas}, $volume_id);
            $self->{parts_changed} = 1;
            $self->reload_tree($volume_id - 1);
        }
    }
}

sub on_btn_move_down {
    my ($self) = @_;
    my $itemData = $self->get_selection;
    if ($itemData && $itemData->{type} eq 'volume') {
        my $volume_id = $itemData->{volume_id};
        if ($self->{model_object}->move_volume_down($volume_id)) {
            Slic3r::GUI::_3DScene::move_volume_down($self->{canvas}, $volume_id);
            $self->{parts_changed} = 1;
            $self->reload_tree($volume_id + 1);
        }
    }
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

sub on_btn_split {
    my ($self) = @_;

    my $itemData = $self->get_selection;
    if ($itemData && $itemData->{type} eq 'volume') {
        my $volume = $self->{model_object}->volumes->[$itemData->{volume_id}];
        my $nozzle_dmrs = $self->GetParent->GetParent->GetParent->{config}->get('nozzle_diameter');
        $self->{parts_changed} = 1 if $volume->split(scalar(@$nozzle_dmrs)) > 1;
    }
    
    $self->_parts_changed;
}

sub _parts_changed {
    my ($self) = @_;
    
    $self->reload_tree;
    if ($self->{canvas}) {
        Slic3r::GUI::_3DScene::reset_volumes($self->{canvas});
        Slic3r::GUI::_3DScene::load_model_object($self->{canvas}, $self->{model_object}, 0, [0]);
        Slic3r::GUI::_3DScene::zoom_to_volumes($self->{canvas});
        Slic3r::GUI::_3DScene::update_volumes_colors_by_extruder($self->{canvas});
        Slic3r::GUI::_3DScene::render($self->{canvas});        
    }
}

sub CanClose {
    my $self = shift;
    
    return 1;  # skip validation for now
    
    # validate options before allowing user to dismiss the dialog
    # the validate method only works on full configs so we have
    # to merge our settings with the default ones
    my $config = $self->GetParent->GetParent->GetParent->GetParent->GetParent->config->clone;
    eval {
        $config->apply($self->model_object->config);
        $config->validate;
    };
    return ! Slic3r::GUI::catch_error($self);
}

sub Destroy {
    my ($self) = @_;
    $self->{canvas}->Destroy if ($self->{canvas});
}

sub PartsChanged {
    my ($self) = @_;
    return $self->{parts_changed};
}

sub PartSettingsChanged {
    my ($self) = @_;
    return $self->{part_settings_changed};
}

sub _update_canvas {
    my ($self) = @_;
    
    if ($self->{canvas}) {
        Slic3r::GUI::_3DScene::reset_volumes($self->{canvas});
        Slic3r::GUI::_3DScene::load_model_object($self->{canvas}, $self->{model_object}, 0, [0]);

        # restore selection, if any
        if (my $itemData = $self->get_selection) {
            if ($itemData->{type} eq 'volume') {
                Slic3r::GUI::_3DScene::select_volume($self->{canvas}, $itemData->{volume_id});
            }
        }

        Slic3r::GUI::_3DScene::update_volumes_colors_by_extruder($self->{canvas});
        Slic3r::GUI::_3DScene::render($self->{canvas});
    }
}

sub _update {
    my ($self) = @_;
    my ($m_x, $m_y, $m_z) = ($self->{move_options}{x}, $self->{move_options}{y}, $self->{move_options}{z});
    my ($l_x, $l_y, $l_z) = ($self->{last_coords}{x}, $self->{last_coords}{y}, $self->{last_coords}{z});
    
    my $itemData = $self->get_selection;
    if ($itemData && $itemData->{type} eq 'volume') {
        my $d = Slic3r::Pointf3->new($m_x - $l_x, $m_y - $l_y, $m_z - $l_z);
        my $volume = $self->{model_object}->volumes->[$itemData->{volume_id}];
        $volume->mesh->translate(@{$d});
        $self->{last_coords}{x} = $m_x;
        $self->{last_coords}{y} = $m_y;
        $self->{last_coords}{z} = $m_z;
    }

    $self->{parts_changed} = 1;
    my @objects = ();
    push @objects, $self->{model_object};
    Slic3r::GUI::_3DScene::reset_volumes($self->{canvas});
    Slic3r::GUI::_3DScene::load_model_object($self->{canvas}, $_, 0, [0]) for @objects;
    Slic3r::GUI::_3DScene::update_volumes_colors_by_extruder($self->{canvas});
    Slic3r::GUI::_3DScene::render($self->{canvas});
}

1;
