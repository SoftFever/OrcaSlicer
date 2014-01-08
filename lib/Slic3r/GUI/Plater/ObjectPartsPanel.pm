package Slic3r::GUI::Plater::ObjectPartsPanel;
use strict;
use warnings;
use utf8;

use Wx qw(:misc :sizer :treectrl wxTAB_TRAVERSAL wxSUNKEN_BORDER wxBITMAP_TYPE_PNG);
use Wx::Event qw(EVT_BUTTON EVT_TREE_ITEM_COLLAPSING);
use base 'Wx::Panel';

use constant ICON_MATERIAL      => 0;
use constant ICON_SOLIDMESH     => 1;
use constant ICON_MODIFIERMESH  => 2;

sub new {
    my $class = shift;
    my ($parent, %params) = @_;
    my $self = $class->SUPER::new($parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    
    my $object = $self->{model_object} = $params{model_object};
    
    $self->{sizer} = Wx::BoxSizer->new(wxVERTICAL);
    
    # create TreeCtrl
    my $tree = $self->{tree} = Wx::TreeCtrl->new($self, -1, wxDefaultPosition, [-1, 200], 
        wxTR_NO_BUTTONS | wxSUNKEN_BORDER | wxTR_HAS_VARIABLE_ROW_HEIGHT | wxTR_HIDE_ROOT
        | wxTR_MULTIPLE | wxTR_NO_BUTTONS);
    {
        $self->{tree_icons} = Wx::ImageList->new(16, 16, 1);
        $tree->AssignImageList($self->{tree_icons});
        $self->{tree_icons}->Add(Wx::Bitmap->new("$Slic3r::var/tag_blue.png", wxBITMAP_TYPE_PNG));
        $self->{tree_icons}->Add(Wx::Bitmap->new("$Slic3r::var/package.png", wxBITMAP_TYPE_PNG));
        $self->{tree_icons}->Add(Wx::Bitmap->new("$Slic3r::var/package_green.png", wxBITMAP_TYPE_PNG));
        
        my $rootId = $tree->AddRoot("");
        my %nodes = ();  # material_id => nodeId
        foreach my $volume (@{$object->volumes}) {
            my $material_id = $volume->material_id;
            $material_id //= '_';
            
            if (!exists $nodes{$material_id}) {
                $nodes{$material_id} = $tree->AppendItem($rootId, $object->model->get_material_name($material_id), ICON_MATERIAL);
            }
            my $name = $volume->modifier ? 'Modifier mesh' : 'Solid mesh';
            my $icon = $volume->modifier ? ICON_MODIFIERMESH : ICON_SOLIDMESH;
            $tree->AppendItem($nodes{$material_id}, $name, $icon);
        }
        $tree->ExpandAll;
    }
    EVT_TREE_ITEM_COLLAPSING($self, $tree, sub {
        my ($self, $event) = @_;
        $event->Veto;
    });
    
    $self->{sizer}->Add($tree, 0, wxEXPAND | wxALL, 10);
    
    $self->SetSizer($self->{sizer});
    $self->{sizer}->SetSizeHints($self);
    return $self;
}

1;
