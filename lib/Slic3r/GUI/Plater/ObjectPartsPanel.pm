package Slic3r::GUI::Plater::ObjectPartsPanel;
use strict;
use warnings;
use utf8;

use File::Basename qw(basename);
use Wx qw(:misc :sizer :treectrl :button wxTAB_TRAVERSAL wxSUNKEN_BORDER wxBITMAP_TYPE_PNG);
use Wx::Event qw(EVT_BUTTON EVT_TREE_ITEM_COLLAPSING EVT_TREE_SEL_CHANGED);
use base 'Wx::Panel';

use constant ICON_MATERIAL      => 0;
use constant ICON_SOLIDMESH     => 1;
use constant ICON_MODIFIERMESH  => 2;

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    
    my $object = $self->{model_object} = $params{model_object};
    
    # create TreeCtrl
    my $tree = $self->{tree} = Wx::TreeCtrl->new($self, -1, wxDefaultPosition, [300, 100], 
        wxTR_NO_BUTTONS | wxSUNKEN_BORDER | wxTR_HAS_VARIABLE_ROW_HEIGHT | wxTR_HIDE_ROOT
        | wxTR_MULTIPLE | wxTR_NO_BUTTONS | wxTR_NO_LINES);
    {
        $self->{tree_icons} = Wx::ImageList->new(16, 16, 1);
        $tree->AssignImageList($self->{tree_icons});
        $self->{tree_icons}->Add(Wx::Bitmap->new("$Slic3r::var/tag_blue.png", wxBITMAP_TYPE_PNG));
        $self->{tree_icons}->Add(Wx::Bitmap->new("$Slic3r::var/package.png", wxBITMAP_TYPE_PNG));
        $self->{tree_icons}->Add(Wx::Bitmap->new("$Slic3r::var/package_green.png", wxBITMAP_TYPE_PNG));
        
        $tree->AddRoot("");
        $self->reload_tree;
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
    $self->{settings_panel} = Slic3r::GUI::Plater::OverrideSettingsPanel->new(
        $self,
        opt_keys => Slic3r::Config::PrintRegion->new->get_keys,
    );
    my $settings_sizer = Wx::StaticBoxSizer->new(Wx::StaticBox->new($self, -1, "Part Settings"), wxVERTICAL);
    $settings_sizer->Add($self->{settings_panel}, 1, wxEXPAND | wxALL, 0);
    
    # left pane with tree
    my $left_sizer = Wx::BoxSizer->new(wxVERTICAL);
    $left_sizer->Add($tree, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    $left_sizer->Add($buttons_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    $left_sizer->Add($settings_sizer, 1, wxEXPAND | wxALL, 0);
    
    # right pane with preview canvas
    my $canvas;
    if ($Slic3r::GUI::have_OpenGL) {
        $canvas = $self->{canvas} = Slic3r::GUI::PreviewCanvas->new($self, $self->{model_object});
        $canvas->SetSize([500,500]);
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
        $self->selection_changed;
    });
    EVT_BUTTON($self, $self->{btn_load_part}, sub { $self->on_btn_load(0) });
    EVT_BUTTON($self, $self->{btn_load_modifier}, sub { $self->on_btn_load(1) });
    EVT_BUTTON($self, $self->{btn_delete}, \&on_btn_delete);
    
    $self->selection_changed;
    
    return $self;
}

sub reload_tree {
    my ($self) = @_;
    
    my $object  = $self->{model_object};
    my $tree    = $self->{tree};
    my $rootId  = $tree->GetRootItem;
    
    $tree->DeleteChildren($rootId);
    
    foreach my $volume_id (0..$#{$object->volumes}) {
        my $volume = $object->volumes->[$volume_id];
        
        my $material_id = $volume->material_id // '_';
        my $material_name = $material_id eq '_'
            ? sprintf("Part #%d", $volume_id+1)
            : $object->model->get_material_name($material_id);
        
        my $icon = $volume->modifier ? ICON_MODIFIERMESH : ICON_SOLIDMESH;
        my $itemId = $tree->AppendItem($rootId, $material_name, $icon);
        $tree->SetPlData($itemId, {
            type        => 'volume',
            volume_id   => $volume_id,
        });
    }
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
        $_->{selected} = 0 for @{$self->{canvas}->volumes};
    }
    
    # disable things as if nothing is selected
    $self->{btn_delete}->Disable;
    $self->{settings_panel}->Disable;
    
    my $itemData = $self->get_selection;
    if ($itemData && $itemData->{type} eq 'volume') {
        if ($self->{canvas}) {
            $self->{canvas}->volumes->[ $itemData->{volume_id} ]{selected} = 1;
        }
        $self->{btn_delete}->Enable;
        
        my $volume = $self->{model_object}->volumes->[ $itemData->{volume_id} ];
        my $material = $self->{model_object}->model->materials->{ $volume->material_id // '_' };
        $material //= $volume->assign_unique_material;
        $self->{settings_panel}->Enable;
        $self->{settings_panel}->set_config($material->config);
    }
    
    $self->{canvas}->Render if $self->{canvas};
}

sub on_btn_load {
    my ($self, $is_modifier) = @_;
    
    my @input_files = Slic3r::GUI::open_model($self);
    foreach my $input_file (@input_files) {
        my $model = eval { Slic3r::Model->read_from_file($input_file) };
        if ($@) {
            Slic3r::GUI::show_error($self, $@);
            next;
        }
        
        foreach my $object (@{$model->objects}) {
            foreach my $volume (@{$object->volumes}) {
                my $new_volume = $self->{model_object}->add_volume($volume);
                $new_volume->modifier($is_modifier);
                if (!defined $new_volume->material_id) {
                    my $material_name = basename($input_file);
                    $material_name =~ s/\.(stl|obj)$//i;
                    $self->{model_object}->model->set_material($material_name);
                    $new_volume->material_id($material_name);
                }
            }
        }
    }
    
    $self->reload_tree;
    if ($self->{canvas}) {
        $self->{canvas}->load_object($self->{model_object});
        $self->{canvas}->Render;
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
    }
    
    $self->reload_tree;
    if ($self->{canvas}) {
        $self->{canvas}->load_object($self->{model_object});
        $self->{canvas}->Render;
    }
}

1;
