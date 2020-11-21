#include "ObjectDataViewModel.hpp"
#include "wxExtensions.hpp"
#include "BitmapCache.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "I18N.hpp"

#include "libslic3r/Model.hpp"

#include <wx/bmpcbox.h>
#include <wx/dc.h>


namespace Slic3r {

namespace GUI {

wxDEFINE_EVENT(wxCUSTOMEVT_LAST_VOLUME_IS_DELETED, wxCommandEvent);

static wxBitmap get_extruder_color_icon(size_t extruder_idx, bool thin_icon = false)
{
    // Create the bitmap with color bars.
    std::vector<wxBitmap*> bmps = get_extruder_color_icons(thin_icon);
    if (bmps.empty())
        return wxNullBitmap;

    return *bmps[extruder_idx >= bmps.size() ? 0 : extruder_idx];
}

BitmapCache* m_bitmap_cache = nullptr;

// *****************************************************************************
// ----------------------------------------------------------------------------
// ObjectDataViewModelNode
// ----------------------------------------------------------------------------

void ObjectDataViewModelNode::init_container()
{
#ifdef __WXGTK__
    // it's necessary on GTK because of control have to know if this item will be container
    // in another case you couldn't to add subitem for this item
    // it will be produce "segmentation fault"
    m_container = true;
#endif  //__WXGTK__
}

#define LAYER_ROOT_ICON "edit_layers_all"
#define LAYER_ICON      "edit_layers_some"

ObjectDataViewModelNode::ObjectDataViewModelNode(ObjectDataViewModelNode* parent, const ItemType type) :
    m_parent(parent),
    m_type(type),
    m_extruder(wxEmptyString)
{
    if (type == itSettings)
        m_name = "Settings to modified";
    else if (type == itInstanceRoot)
        m_name = _(L("Instances"));
    else if (type == itInstance)
    {
        m_idx = parent->GetChildCount();
        m_name = wxString::Format(_(L("Instance %d")), m_idx + 1);

        set_action_and_extruder_icons();
    }
    else if (type == itLayerRoot)
    {
        m_bmp = create_scaled_bitmap(LAYER_ROOT_ICON);    // FIXME: pass window ptr
        m_name = _(L("Layers"));
    }

    if (type & (itInstanceRoot | itLayerRoot))
        init_container();
}

ObjectDataViewModelNode::ObjectDataViewModelNode(ObjectDataViewModelNode* parent, 
                                                 const t_layer_height_range& layer_range,
                                                 const int idx /*= -1 */, 
                                                 const wxString& extruder) :
    m_parent(parent),
    m_type(itLayer),
    m_idx(idx),
    m_layer_range(layer_range),
    m_extruder(extruder)
{
    const int children_cnt = parent->GetChildCount();
    if (idx < 0)
        m_idx = children_cnt;
    else
    {
        // update indexes for another Laeyr Nodes
        for (int i = m_idx; i < children_cnt; i++)
            parent->GetNthChild(i)->SetIdx(i + 1);
    }
    const std::string label_range = (boost::format(" %.2f-%.2f ") % layer_range.first % layer_range.second).str();
    m_name = _(L("Range")) + label_range + "(" + _(L("mm")) + ")";
    m_bmp = create_scaled_bitmap(LAYER_ICON);    // FIXME: pass window ptr

    set_action_and_extruder_icons();
    init_container();
}

#ifndef NDEBUG
bool ObjectDataViewModelNode::valid()
{
	// Verify that the object was not deleted yet.
	assert(m_idx >= -1);
	return m_idx >= -1;
}
#endif /* NDEBUG */

void ObjectDataViewModelNode::set_action_and_extruder_icons()
{
    m_action_icon_name = m_type & itObject              ? "advanced_plus" : 
                         m_type & (itVolume | itLayer)  ? "cog" : /*m_type & itInstance*/ "set_separate_obj";
    m_action_icon = create_scaled_bitmap(m_action_icon_name);    // FIXME: pass window ptr

    // set extruder bitmap
    set_extruder_icon();
}

void ObjectDataViewModelNode::set_extruder_icon()
{
    if (m_type & itInstance)
        return; // don't set colored bitmap for Instance

    int extruder_idx = atoi(m_extruder.c_str());
    if (extruder_idx > 0) --extruder_idx;
    m_extruder_bmp = get_extruder_color_icon(extruder_idx);
}

void ObjectDataViewModelNode::set_printable_icon(PrintIndicator printable)
{
    m_printable = printable;
    m_printable_icon = m_printable == piUndef ? m_empty_bmp :
                       create_scaled_bitmap(m_printable == piPrintable ? "eye_open.png" : "eye_closed.png");
}

void ObjectDataViewModelNode::update_settings_digest_bitmaps()
{
    m_bmp = m_empty_bmp;

    std::map<std::string, wxBitmap>& categories_icon = Slic3r::GUI::wxGetApp().obj_list()->CATEGORY_ICON;

    std::string scaled_bitmap_name = m_name.ToUTF8().data();
    scaled_bitmap_name += "-em" + std::to_string(wxGetApp().em_unit()) + (wxGetApp().dark_mode() ? "-dm" : "");

    wxBitmap *bmp = m_bitmap_cache->find(scaled_bitmap_name);
    if (bmp == nullptr) {
        std::vector<wxBitmap> bmps;
        for (auto& cat : m_opt_categories)
            bmps.emplace_back(  categories_icon.find(cat) == categories_icon.end() ?
                                wxNullBitmap : categories_icon.at(cat));
        bmp = m_bitmap_cache->insert(scaled_bitmap_name, bmps);
    }

    m_bmp = *bmp;
}

bool ObjectDataViewModelNode::update_settings_digest(const std::vector<std::string>& categories)
{
    if (m_type != itSettings || m_opt_categories == categories)
        return false;

    m_opt_categories = categories;
    m_name = wxEmptyString;

    for (auto& cat : m_opt_categories)
        m_name += _(cat) + "; ";
    if (!m_name.IsEmpty())
        m_name.erase(m_name.Length()-2, 2); // Delete last "; "

    update_settings_digest_bitmaps();

    return true;
}

void ObjectDataViewModelNode::msw_rescale()
{
    if (!m_action_icon_name.empty())
        m_action_icon = create_scaled_bitmap(m_action_icon_name);

    if (m_printable != piUndef)
        m_printable_icon = create_scaled_bitmap(m_printable == piPrintable ? "eye_open.png" : "eye_closed.png");

    if (!m_opt_categories.empty())
        update_settings_digest_bitmaps();

    set_extruder_icon();
}

bool ObjectDataViewModelNode::SetValue(const wxVariant& variant, unsigned col)
{
    switch (col)
    {
    case colPrint:
        m_printable_icon << variant;
        return true;
    case colName: {
        DataViewBitmapText data;
        data << variant;
        m_bmp = data.GetBitmap();
        m_name = data.GetText();
        return true; }
    case colExtruder: {
        DataViewBitmapText data;
        data << variant;
        m_extruder_bmp = data.GetBitmap();
        m_extruder = data.GetText() == "0" ? _(L("default")) : data.GetText();
        return true; }
    case colEditing:
        m_action_icon << variant;
        return true;
    default:
        printf("MyObjectTreeModel::SetValue: wrong column");
    }
    return false;
}

void ObjectDataViewModelNode::SetIdx(const int& idx)
{
    m_idx = idx;
    // update name if this node is instance
    if (m_type == itInstance)
        m_name = wxString::Format(_(L("Instance %d")), m_idx + 1);
}

// *****************************************************************************
// ----------------------------------------------------------------------------
// ObjectDataViewModel
// ----------------------------------------------------------------------------

static int get_root_idx(ObjectDataViewModelNode *parent_node, const ItemType root_type)
{
    // because of istance_root and layers_root are at the end of the list, so
    // start locking from the end
    for (int root_idx = parent_node->GetChildCount() - 1; root_idx >= 0; root_idx--)
    {
        // if there is SettingsItem or VolumeItem, then RootItems don't exist in current ObjectItem 
        if (parent_node->GetNthChild(root_idx)->GetType() & (itSettings | itVolume))
            break;
        if (parent_node->GetNthChild(root_idx)->GetType() & root_type)
            return root_idx;
    }

    return -1;
}

ObjectDataViewModel::ObjectDataViewModel()
{
    m_bitmap_cache = new Slic3r::GUI::BitmapCache;
}

ObjectDataViewModel::~ObjectDataViewModel()
{
    for (auto object : m_objects)
			delete object;
    delete m_bitmap_cache;
    m_bitmap_cache = nullptr;
}

wxDataViewItem ObjectDataViewModel::Add(const wxString &name, 
                                        const int extruder,
                                        const bool has_errors/* = false*/)
{
    const wxString extruder_str = extruder == 0 ? _(L("default")) : wxString::Format("%d", extruder);
	auto root = new ObjectDataViewModelNode(name, extruder_str);
    // Add error icon if detected auto-repaire
    if (has_errors)
        root->m_bmp = *m_warning_bmp;

    m_objects.push_back(root);
	// notify control
	wxDataViewItem child((void*)root);
	wxDataViewItem parent((void*)NULL);

	ItemAdded(parent, child);
	return child;
}

wxDataViewItem ObjectDataViewModel::AddVolumeChild( const wxDataViewItem &parent_item,
                                                    const wxString &name,
                                                    const Slic3r::ModelVolumeType volume_type,
                                                    const bool has_errors/* = false*/,
                                                    const int extruder/* = 0*/,
                                                    const bool create_frst_child/* = true*/)
{
	ObjectDataViewModelNode *root = (ObjectDataViewModelNode*)parent_item.GetID();
	if (!root) return wxDataViewItem(0);

    wxString extruder_str = extruder == 0 ? _(L("default")) : wxString::Format("%d", extruder);

    // get insertion position according to the existed Layers and/or Instances Items
    int insert_position = get_root_idx(root, itLayerRoot);
    if (insert_position < 0)
        insert_position = get_root_idx(root, itInstanceRoot);

    const bool obj_errors = root->m_bmp.IsOk();

    if (create_frst_child && root->m_volumes_cnt == 0)
    {
        const Slic3r::ModelVolumeType type = Slic3r::ModelVolumeType::MODEL_PART;
        const auto node = new ObjectDataViewModelNode(root, root->m_name, GetVolumeIcon(type, obj_errors), extruder_str, 0);
        node->m_volume_type = type;

        insert_position < 0 ? root->Append(node) : root->Insert(node, insert_position);
		// notify control
		const wxDataViewItem child((void*)node);
		ItemAdded(parent_item, child);

        root->m_volumes_cnt++;
        if (insert_position >= 0) insert_position++;
	}

    const auto node = new ObjectDataViewModelNode(root, name, GetVolumeIcon(volume_type, has_errors), extruder_str, root->m_volumes_cnt);
    insert_position < 0 ? root->Append(node) : root->Insert(node, insert_position);

    // if part with errors is added, but object wasn't marked, then mark it
    if (!obj_errors && has_errors)
        root->SetBitmap(*m_warning_bmp);

	// notify control
    const wxDataViewItem child((void*)node);
    ItemAdded(parent_item, child);
    root->m_volumes_cnt++;

    node->m_volume_type = volume_type;

	return child;
}

wxDataViewItem ObjectDataViewModel::AddSettingsChild(const wxDataViewItem &parent_item)
{
    ObjectDataViewModelNode *root = (ObjectDataViewModelNode*)parent_item.GetID();
    if (!root) return wxDataViewItem(0);

    const auto node = new ObjectDataViewModelNode(root, itSettings);
    root->Insert(node, 0);
    // notify control
    const wxDataViewItem child((void*)node);
    ItemAdded(parent_item, child);
    return child;
}

/* return values:
 * true     => root_node is created and added to the parent_root
 * false    => root node alredy exists
*/
static bool append_root_node(ObjectDataViewModelNode *parent_node, 
                             ObjectDataViewModelNode **root_node, 
                             const ItemType root_type)
{
    const int inst_root_id = get_root_idx(parent_node, root_type);

    *root_node = inst_root_id < 0 ?
                new ObjectDataViewModelNode(parent_node, root_type) :
                parent_node->GetNthChild(inst_root_id);
    
    if (inst_root_id < 0) {
        if ((root_type&itInstanceRoot) ||
            ( (root_type&itLayerRoot) && get_root_idx(parent_node, itInstanceRoot)<0) )
            parent_node->Append(*root_node);
        else if (root_type&itLayerRoot)
            parent_node->Insert(*root_node, static_cast<unsigned int>(get_root_idx(parent_node, itInstanceRoot)));
        return true;
    }

    return false;
}

wxDataViewItem ObjectDataViewModel::AddRoot(const wxDataViewItem &parent_item, ItemType root_type)
{
    ObjectDataViewModelNode *parent_node = (ObjectDataViewModelNode*)parent_item.GetID();
    if (!parent_node) return wxDataViewItem(0);

    // get InstanceRoot node
    ObjectDataViewModelNode *root_node { nullptr };
    const bool appended = append_root_node(parent_node, &root_node, root_type);
    if (!root_node) return wxDataViewItem(0);

    const wxDataViewItem root_item((void*)root_node);

    if (appended)
        ItemAdded(parent_item, root_item);// notify control
    return root_item;
}

wxDataViewItem ObjectDataViewModel::AddInstanceRoot(const wxDataViewItem &parent_item)
{
    return AddRoot(parent_item, itInstanceRoot);
}

wxDataViewItem ObjectDataViewModel::AddInstanceChild(const wxDataViewItem &parent_item, size_t num)
{
    std::vector<bool> print_indicator(num, true);

    // if InstanceRoot item isn't created for this moment
    if (!GetInstanceRootItem(parent_item).IsOk())
        // use object's printable state to first instance
        print_indicator[0] = IsPrintable(parent_item);
    
    return wxDataViewItem((void*)AddInstanceChild(parent_item, print_indicator));
}

wxDataViewItem ObjectDataViewModel::AddInstanceChild(const wxDataViewItem& parent_item,
                                                     const std::vector<bool>& print_indicator)
{
    const wxDataViewItem inst_root_item = AddInstanceRoot(parent_item);
    if (!inst_root_item) return wxDataViewItem(0);

    ObjectDataViewModelNode* inst_root_node = (ObjectDataViewModelNode*)inst_root_item.GetID();

    // Add instance nodes
    ObjectDataViewModelNode *instance_node = nullptr;    
    size_t counter = 0;
    while (counter < print_indicator.size()) {
        instance_node = new ObjectDataViewModelNode(inst_root_node, itInstance);

        instance_node->set_printable_icon(print_indicator[counter] ? piPrintable : piUnprintable);

        inst_root_node->Append(instance_node);
        // notify control
        const wxDataViewItem instance_item((void*)instance_node);
        ItemAdded(inst_root_item, instance_item);
        ++counter;
    }

    // update object_node printable property
    UpdateObjectPrintable(parent_item);

    return wxDataViewItem((void*)instance_node);
}

void ObjectDataViewModel::UpdateObjectPrintable(wxDataViewItem parent_item)
{
    const wxDataViewItem inst_root_item = GetInstanceRootItem(parent_item);
    if (!inst_root_item) 
        return;

    ObjectDataViewModelNode* inst_root_node = (ObjectDataViewModelNode*)inst_root_item.GetID();

    const size_t child_cnt = inst_root_node->GetChildren().Count();
    PrintIndicator obj_pi = piUnprintable;
    for (size_t i=0; i < child_cnt; i++)
        if (inst_root_node->GetNthChild(i)->IsPrintable() & piPrintable) {
            obj_pi = piPrintable;
            break;
        }
    // and set printable state for object_node to piUndef
    ObjectDataViewModelNode* obj_node = (ObjectDataViewModelNode*)parent_item.GetID();
    obj_node->set_printable_icon(obj_pi);
    ItemChanged(parent_item);
}

// update printable property for all instances from object
void ObjectDataViewModel::UpdateInstancesPrintable(wxDataViewItem parent_item)
{
    const wxDataViewItem inst_root_item = GetInstanceRootItem(parent_item);
    if (!inst_root_item) 
        return;

    ObjectDataViewModelNode* obj_node = (ObjectDataViewModelNode*)parent_item.GetID();
    const PrintIndicator obj_pi = obj_node->IsPrintable();

    ObjectDataViewModelNode* inst_root_node = (ObjectDataViewModelNode*)inst_root_item.GetID();
    const size_t child_cnt = inst_root_node->GetChildren().Count();

    for (size_t i=0; i < child_cnt; i++)
    {
        ObjectDataViewModelNode* inst_node = inst_root_node->GetNthChild(i);
        // and set printable state for object_node to piUndef
        inst_node->set_printable_icon(obj_pi);
        ItemChanged(wxDataViewItem((void*)inst_node));
    }
}

bool ObjectDataViewModel::IsPrintable(const wxDataViewItem& item) const
{
    ObjectDataViewModelNode* node = (ObjectDataViewModelNode*)item.GetID();
    if (!node)
        return false;

    return node->IsPrintable() == piPrintable;
}

wxDataViewItem ObjectDataViewModel::AddLayersRoot(const wxDataViewItem &parent_item)
{
    return AddRoot(parent_item, itLayerRoot);
}

wxDataViewItem ObjectDataViewModel::AddLayersChild(const wxDataViewItem &parent_item, 
                                                   const t_layer_height_range& layer_range,
                                                   const int extruder/* = 0*/, 
                                                   const int index /* = -1*/)
{
    ObjectDataViewModelNode *parent_node = (ObjectDataViewModelNode*)parent_item.GetID();
    if (!parent_node) return wxDataViewItem(0);

    wxString extruder_str = extruder == 0 ? _(L("default")) : wxString::Format("%d", extruder);

    // get LayerRoot node
    ObjectDataViewModelNode *layer_root_node;
    wxDataViewItem layer_root_item;

    if (parent_node->GetType() & itLayerRoot) {
        layer_root_node = parent_node;
        layer_root_item = parent_item;
    }
    else {
        const int root_idx = get_root_idx(parent_node, itLayerRoot);
        if (root_idx < 0) return wxDataViewItem(0);
        layer_root_node = parent_node->GetNthChild(root_idx);
        layer_root_item = wxDataViewItem((void*)layer_root_node);
    }

    // Add layer node
    ObjectDataViewModelNode *layer_node = new ObjectDataViewModelNode(layer_root_node, layer_range, index, extruder_str);
    if (index < 0)
        layer_root_node->Append(layer_node);
    else
        layer_root_node->Insert(layer_node, index);

    // notify control
    const wxDataViewItem layer_item((void*)layer_node);
    ItemAdded(layer_root_item, layer_item);

    return layer_item;
}

wxDataViewItem ObjectDataViewModel::Delete(const wxDataViewItem &item)
{
	auto ret_item = wxDataViewItem(0);
	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	if (!node)      // happens if item.IsOk()==false
		return ret_item;

	auto node_parent = node->GetParent();
	wxDataViewItem parent(node_parent);

	// first remove the node from the parent's array of children;
	// NOTE: MyObjectTreeModelNodePtrArray is only an array of _pointers_
	//       thus removing the node from it doesn't result in freeing it
	if (node_parent) {
        if (node->m_type & (itInstanceRoot|itLayerRoot))
        {
            // node can be deleted by the Delete, let's check its type while we safely can
            bool is_instance_root = (node->m_type & itInstanceRoot);

            for (int i = int(node->GetChildCount() - 1); i >= (is_instance_root ? 1 : 0); i--)
                Delete(wxDataViewItem(node->GetNthChild(i)));

            return parent;
        }

		auto id = node_parent->GetChildren().Index(node);
        auto idx = node->GetIdx();


        if (node->m_type & (itVolume|itLayer)) {
            node_parent->m_volumes_cnt--;
            DeleteSettings(item);
        }
		node_parent->GetChildren().Remove(node);

		if (id > 0) { 
            if (size_t(id) == node_parent->GetChildCount()) id--;
			ret_item = wxDataViewItem(node_parent->GetChildren().Item(id));
		}

		//update idx value for remaining child-nodes
		auto children = node_parent->GetChildren();
        for (size_t i = 0; i < node_parent->GetChildCount() && idx>=0; i++)
		{
            auto cur_idx = children[i]->GetIdx();
			if (cur_idx > idx)
				children[i]->SetIdx(cur_idx-1);
		}

        // if there is last instance item, delete both of it and instance root item
        if (node_parent->GetChildCount() == 1 && node_parent->GetNthChild(0)->m_type == itInstance)
        {
            delete node;
            ItemDeleted(parent, item);

            ObjectDataViewModelNode *last_instance_node = node_parent->GetNthChild(0);
            PrintIndicator last_instance_printable = last_instance_node->IsPrintable();
            node_parent->GetChildren().Remove(last_instance_node);
            delete last_instance_node;
            ItemDeleted(parent, wxDataViewItem(last_instance_node));

            ObjectDataViewModelNode *obj_node = node_parent->GetParent();
            obj_node->set_printable_icon(last_instance_printable);
            obj_node->GetChildren().Remove(node_parent);
            delete node_parent;
            ret_item = wxDataViewItem(obj_node);

#ifndef __WXGTK__
            if (obj_node->GetChildCount() == 0)
                obj_node->m_container = false;
#endif //__WXGTK__
            ItemDeleted(ret_item, wxDataViewItem(node_parent));
            return ret_item;
        }

        if (node->m_type & itInstance)
            UpdateObjectPrintable(wxDataViewItem(node_parent->GetParent()));

        // if there was last layer item, delete this one and layers root item
        if (node_parent->GetChildCount() == 0 && node_parent->m_type == itLayerRoot)
        {
            ObjectDataViewModelNode *obj_node = node_parent->GetParent();
            obj_node->GetChildren().Remove(node_parent);
            delete node_parent;
            ret_item = wxDataViewItem(obj_node);

#ifndef __WXGTK__
            if (obj_node->GetChildCount() == 0)
                obj_node->m_container = false;
#endif //__WXGTK__
            ItemDeleted(ret_item, wxDataViewItem(node_parent));
            return ret_item;
        }

        // if there is last volume item after deleting, delete this last volume too
        if (node_parent->GetChildCount() <= 3) // 3??? #ys_FIXME
        {
            int vol_cnt = 0;
            int vol_idx = 0;
            for (size_t i = 0; i < node_parent->GetChildCount(); ++i) {
                if (node_parent->GetNthChild(i)->GetType() == itVolume) {
                    vol_idx = i;
                    vol_cnt++;
                }
                if (vol_cnt > 1)
                    break;
            }

            if (vol_cnt == 1) {
                delete node;
                ItemDeleted(parent, item);

                ObjectDataViewModelNode *last_child_node = node_parent->GetNthChild(vol_idx);
                DeleteSettings(wxDataViewItem(last_child_node));
                node_parent->GetChildren().Remove(last_child_node);
                node_parent->m_volumes_cnt = 0;
                delete last_child_node;

#ifndef __WXGTK__
                if (node_parent->GetChildCount() == 0)
                    node_parent->m_container = false;
#endif //__WXGTK__
                ItemDeleted(parent, wxDataViewItem(last_child_node));

                wxCommandEvent event(wxCUSTOMEVT_LAST_VOLUME_IS_DELETED);
                auto it = find(m_objects.begin(), m_objects.end(), node_parent);
                event.SetInt(it == m_objects.end() ? -1 : it - m_objects.begin());
                wxPostEvent(m_ctrl, event);

                ret_item = parent;

                return ret_item;
            }
        }
	}
	else
	{
		auto it = find(m_objects.begin(), m_objects.end(), node);
        size_t id = it - m_objects.begin();
		if (it != m_objects.end())
		{
            // Delete all sub-items
            int i = m_objects[id]->GetChildCount() - 1;
            while (i >= 0) {
                Delete(wxDataViewItem(m_objects[id]->GetNthChild(i)));
                i = m_objects[id]->GetChildCount() - 1;
            }
			m_objects.erase(it);
        }
		if (id > 0) { 
			if(id == m_objects.size()) id--;
			ret_item = wxDataViewItem(m_objects[id]);
		}
	}
	// free the node
	delete node;

	// set m_containet to FALSE if parent has no child
	if (node_parent) {
#ifndef __WXGTK__
        if (node_parent->GetChildCount() == 0)
            node_parent->m_container = false;
#endif //__WXGTK__
		ret_item = parent;
	}

	// notify control
	ItemDeleted(parent, item);
	return ret_item;
}

wxDataViewItem ObjectDataViewModel::DeleteLastInstance(const wxDataViewItem &parent_item, size_t num)
{
    auto ret_item = wxDataViewItem(0);
    ObjectDataViewModelNode *parent_node = (ObjectDataViewModelNode*)parent_item.GetID();
    if (!parent_node) return ret_item;

    const int inst_root_id = get_root_idx(parent_node, itInstanceRoot);
    if (inst_root_id < 0) return ret_item;

    wxDataViewItemArray items;
    ObjectDataViewModelNode *inst_root_node = parent_node->GetNthChild(inst_root_id);
    const wxDataViewItem inst_root_item((void*)inst_root_node);

    const int inst_cnt = inst_root_node->GetChildCount();
    const bool delete_inst_root_item = inst_cnt - num < 2 ? true : false;

    PrintIndicator last_inst_printable = piUndef;

    int stop = delete_inst_root_item ? 0 : inst_cnt - num;
    for (int i = inst_cnt - 1; i >= stop;--i) {
        ObjectDataViewModelNode *last_instance_node = inst_root_node->GetNthChild(i);
        if (i==0) last_inst_printable = last_instance_node->IsPrintable();
        inst_root_node->GetChildren().Remove(last_instance_node);
        delete last_instance_node;
        ItemDeleted(inst_root_item, wxDataViewItem(last_instance_node));
    }

    if (delete_inst_root_item) {
        ret_item = parent_item;
        parent_node->GetChildren().Remove(inst_root_node);
        parent_node->set_printable_icon(last_inst_printable);
        ItemDeleted(parent_item, inst_root_item);
        ItemChanged(parent_item);
#ifndef __WXGTK__
        if (parent_node->GetChildCount() == 0)
            parent_node->m_container = false;
#endif //__WXGTK__
    }

    // update object_node printable property
    UpdateObjectPrintable(parent_item);

    return ret_item;
}

void ObjectDataViewModel::DeleteAll()
{
	while (!m_objects.empty())
	{
		auto object = m_objects.back();
// 		object->RemoveAllChildren();
		Delete(wxDataViewItem(object));	
	}
}

void ObjectDataViewModel::DeleteChildren(wxDataViewItem& parent)
{
    ObjectDataViewModelNode *root = (ObjectDataViewModelNode*)parent.GetID();
    if (!root)      // happens if item.IsOk()==false
        return;

    // first remove the node from the parent's array of children;
    // NOTE: MyObjectTreeModelNodePtrArray is only an array of _pointers_
    //       thus removing the node from it doesn't result in freeing it
    auto& children = root->GetChildren();
    for (int id = root->GetChildCount() - 1; id >= 0; --id)
    {
        auto node = children[id];
        auto item = wxDataViewItem(node);
        children.RemoveAt(id);

        if (node->m_type == itVolume)
            root->m_volumes_cnt--;

        // free the node
        delete node;

        // notify control
        ItemDeleted(parent, item);
    }

    // set m_containet to FALSE if parent has no child
#ifndef __WXGTK__
        root->m_container = false;
#endif //__WXGTK__
}

void ObjectDataViewModel::DeleteVolumeChildren(wxDataViewItem& parent)
{
    ObjectDataViewModelNode *root = (ObjectDataViewModelNode*)parent.GetID();
    if (!root)      // happens if item.IsOk()==false
        return;

    // first remove the node from the parent's array of children;
    // NOTE: MyObjectTreeModelNodePtrArray is only an array of _pointers_
    //       thus removing the node from it doesn't result in freeing it
    auto& children = root->GetChildren();
    for (int id = root->GetChildCount() - 1; id >= 0; --id)
    {
        auto node = children[id];
        if (node->m_type != itVolume)
            continue;

        auto item = wxDataViewItem(node);
        DeleteSettings(item);
        children.RemoveAt(id);

        // free the node
        delete node;

        // notify control
        ItemDeleted(parent, item);
    }
    root->m_volumes_cnt = 0;

    // set m_containet to FALSE if parent has no child
#ifndef __WXGTK__
    root->m_container = false;
#endif //__WXGTK__
}

void ObjectDataViewModel::DeleteSettings(const wxDataViewItem& parent)
{
    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)parent.GetID();
    if (!node) return;

    // if volume has a "settings"item, than delete it before volume deleting
    if (node->GetChildCount() > 0 && node->GetNthChild(0)->GetType() == itSettings) {
        auto settings_node = node->GetNthChild(0);
        auto settings_item = wxDataViewItem(settings_node);
        node->GetChildren().RemoveAt(0);
        delete settings_node;
        ItemDeleted(parent, settings_item);
    }
}

wxDataViewItem ObjectDataViewModel::GetItemById(int obj_idx)
{
    if (size_t(obj_idx) >= m_objects.size())
	{
		printf("Error! Out of objects range.\n");
		return wxDataViewItem(0);
	}
	return wxDataViewItem(m_objects[obj_idx]);
}


wxDataViewItem ObjectDataViewModel::GetItemByVolumeId(int obj_idx, int volume_idx)
{
    if (size_t(obj_idx) >= m_objects.size()) {
		printf("Error! Out of objects range.\n");
		return wxDataViewItem(0);
	}

    auto parent = m_objects[obj_idx];
    if (parent->GetChildCount() == 0 ||
        (parent->GetChildCount() == 1 && parent->GetNthChild(0)->GetType() & itSettings )) {
        if (volume_idx == 0)
            return GetItemById(obj_idx);

        printf("Error! Object has no one volume.\n");
        return wxDataViewItem(0);
    }

    for (size_t i = 0; i < parent->GetChildCount(); i++)
        if (parent->GetNthChild(i)->m_idx == volume_idx && parent->GetNthChild(i)->GetType() & itVolume)
            return wxDataViewItem(parent->GetNthChild(i));

    return wxDataViewItem(0);
}

wxDataViewItem ObjectDataViewModel::GetItemById(const int obj_idx, const int sub_obj_idx, const ItemType parent_type)
{
    if (size_t(obj_idx) >= m_objects.size()) {
        printf("Error! Out of objects range.\n");
        return wxDataViewItem(0);
    }

    auto item = GetItemByType(wxDataViewItem(m_objects[obj_idx]), parent_type);
    if (!item)
        return wxDataViewItem(0);

    auto parent = (ObjectDataViewModelNode*)item.GetID();
    for (size_t i = 0; i < parent->GetChildCount(); i++)
        if (parent->GetNthChild(i)->m_idx == sub_obj_idx)
            return wxDataViewItem(parent->GetNthChild(i));

    return wxDataViewItem(0);
}

wxDataViewItem ObjectDataViewModel::GetItemByInstanceId(int obj_idx, int inst_idx)
{
    return GetItemById(obj_idx, inst_idx, itInstanceRoot);
}

wxDataViewItem ObjectDataViewModel::GetItemByLayerId(int obj_idx, int layer_idx)
{
    return GetItemById(obj_idx, layer_idx, itLayerRoot);
}

wxDataViewItem ObjectDataViewModel::GetItemByLayerRange(const int obj_idx, const t_layer_height_range& layer_range)
{
    if (size_t(obj_idx) >= m_objects.size()) {
        printf("Error! Out of objects range.\n");
        return wxDataViewItem(0);
    }

    auto item = GetItemByType(wxDataViewItem(m_objects[obj_idx]), itLayerRoot);
    if (!item)
        return wxDataViewItem(0);

    auto parent = (ObjectDataViewModelNode*)item.GetID();
    for (size_t i = 0; i < parent->GetChildCount(); i++)
        if (parent->GetNthChild(i)->m_layer_range == layer_range)
            return wxDataViewItem(parent->GetNthChild(i));

    return wxDataViewItem(0);
}

int  ObjectDataViewModel::GetItemIdByLayerRange(const int obj_idx, const t_layer_height_range& layer_range)
{
    wxDataViewItem item = GetItemByLayerRange(obj_idx, layer_range);
    if (!item)
        return -1;

    return GetLayerIdByItem(item);
}

int ObjectDataViewModel::GetIdByItem(const wxDataViewItem& item) const
{
	if(!item.IsOk())
        return -1;

	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	auto it = find(m_objects.begin(), m_objects.end(), node);
	if (it == m_objects.end())
		return -1;

	return it - m_objects.begin();
}

int ObjectDataViewModel::GetIdByItemAndType(const wxDataViewItem& item, const ItemType type) const
{
	wxASSERT(item.IsOk());

	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	if (!node || node->m_type != type)
		return -1;
	return node->GetIdx();
}

int ObjectDataViewModel::GetObjectIdByItem(const wxDataViewItem& item) const
{
    return GetIdByItem(GetTopParent(item));
}

int ObjectDataViewModel::GetVolumeIdByItem(const wxDataViewItem& item) const
{
    return GetIdByItemAndType(item, itVolume);
}

int ObjectDataViewModel::GetInstanceIdByItem(const wxDataViewItem& item) const 
{
    return GetIdByItemAndType(item, itInstance);
}

int ObjectDataViewModel::GetLayerIdByItem(const wxDataViewItem& item) const 
{
    return GetIdByItemAndType(item, itLayer);
}

t_layer_height_range ObjectDataViewModel::GetLayerRangeByItem(const wxDataViewItem& item) const
{
    wxASSERT(item.IsOk());

    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
    if (!node || node->m_type != itLayer)
        return { 0.0f, 0.0f };
    return node->GetLayerRange();
}

bool ObjectDataViewModel::UpdateColumValues(unsigned col)
{
    switch (col)
    {
    case colPrint:
    case colName:
    case colEditing:
        return true;
    case colExtruder:
    {
        wxDataViewItemArray items;
        GetAllChildren(wxDataViewItem(nullptr), items);

        if (items.IsEmpty()) return false;

        for (auto item : items)
            UpdateExtruderBitmap(item);

        return true;
    }
    default:
        printf("MyObjectTreeModel::SetValue: wrong column");
    }
    return false;
}


void ObjectDataViewModel::UpdateExtruderBitmap(wxDataViewItem item)
{
    wxString extruder = GetExtruder(item);
    if (extruder.IsEmpty())
        return;

    // set extruder bitmap
    int extruder_idx = atoi(extruder.c_str());
    if (extruder_idx > 0) --extruder_idx;

    const DataViewBitmapText extruder_val(extruder, get_extruder_color_icon(extruder_idx));

    wxVariant value;
    value << extruder_val;

    SetValue(value, item, colExtruder);
}

void ObjectDataViewModel::GetItemInfo(const wxDataViewItem& item, ItemType& type, int& obj_idx, int& idx)
{
    wxASSERT(item.IsOk());
    type = itUndef;

    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
    if (!node || 
        node->GetIdx() <-1 || 
        ( node->GetIdx() == -1 && 
         !(node->GetType() & (itObject | itSettings | itInstanceRoot | itLayerRoot/* | itLayer*/))
        )
       )
        return;

    idx = node->GetIdx();
    type = node->GetType();

    ObjectDataViewModelNode *parent_node = node->GetParent();
    if (!parent_node) return;

    // get top parent (Object) node
    while (parent_node->m_type != itObject)
        parent_node = parent_node->GetParent();

    auto it = find(m_objects.begin(), m_objects.end(), parent_node);
    if (it != m_objects.end())
        obj_idx = it - m_objects.begin();
    else
        type = itUndef;
}

int ObjectDataViewModel::GetRowByItem(const wxDataViewItem& item) const
{
    if (m_objects.empty())
        return -1;

    int row_num = 0;
    
    for (size_t i = 0; i < m_objects.size(); i++)
    {
        row_num++;
        if (item == wxDataViewItem(m_objects[i]))
            return row_num;

        for (size_t j = 0; j < m_objects[i]->GetChildCount(); j++)
        {
            row_num++;
            ObjectDataViewModelNode* cur_node = m_objects[i]->GetNthChild(j);
            if (item == wxDataViewItem(cur_node))
                return row_num;

            if (cur_node->m_type == itVolume && cur_node->GetChildCount() == 1)
                row_num++;
            if (cur_node->m_type == itInstanceRoot)
            {
                row_num++;
                for (size_t t = 0; t < cur_node->GetChildCount(); t++)
                {
                    row_num++;
                    if (item == wxDataViewItem(cur_node->GetNthChild(t)))
                        return row_num;
                }
            }
        }        
    }

    return -1;
}

bool ObjectDataViewModel::InvalidItem(const wxDataViewItem& item)
{
    if (!item)
        return true;

    ObjectDataViewModelNode* node = (ObjectDataViewModelNode*)item.GetID();
    if (!node || node->invalid()) 
        return true;

    return false;
}

wxString ObjectDataViewModel::GetName(const wxDataViewItem &item) const
{
	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	if (!node)      // happens if item.IsOk()==false
		return wxEmptyString;

	return node->m_name;
}

wxBitmap& ObjectDataViewModel::GetBitmap(const wxDataViewItem &item) const
{
    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
    return node->m_bmp;
}

wxString ObjectDataViewModel::GetExtruder(const wxDataViewItem& item) const
{
	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	if (!node)      // happens if item.IsOk()==false
		return wxEmptyString;

	return node->m_extruder;
}

int ObjectDataViewModel::GetExtruderNumber(const wxDataViewItem& item) const
{
	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	if (!node)      // happens if item.IsOk()==false
		return 0;

	return atoi(node->m_extruder.c_str());
}

void ObjectDataViewModel::GetValue(wxVariant &variant, const wxDataViewItem &item, unsigned int col) const
{
	wxASSERT(item.IsOk());

	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	switch (col)
	{
	case colPrint:
		variant << node->m_printable_icon;
		break;
	case colName:
        variant << DataViewBitmapText(node->m_name, node->m_bmp);
		break;
	case colExtruder:
		variant << DataViewBitmapText(node->m_extruder, node->m_extruder_bmp);
		break;
	case colEditing:
		variant << node->m_action_icon;
		break;
	default:
		;
	}
}

bool ObjectDataViewModel::SetValue(const wxVariant &variant, const wxDataViewItem &item, unsigned int col)
{
	wxASSERT(item.IsOk());

	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	return node->SetValue(variant, col);
}

bool ObjectDataViewModel::SetValue(const wxVariant &variant, const int item_idx, unsigned int col)
{
    if (size_t(item_idx) >= m_objects.size())
		return false;

	return m_objects[item_idx]->SetValue(variant, col);
}

void ObjectDataViewModel::SetExtruder(const wxString& extruder, wxDataViewItem item)
{
    DataViewBitmapText extruder_val;
    extruder_val.SetText(extruder);

    // set extruder bitmap
    int extruder_idx = atoi(extruder.c_str());
    if (extruder_idx > 0) --extruder_idx;
    extruder_val.SetBitmap(get_extruder_color_icon(extruder_idx));

    wxVariant value;
    value << extruder_val;
    
    SetValue(value, item, colExtruder);
}

void ObjectDataViewModel::AddAllChildren(const wxDataViewItem& parent)
{
    ObjectDataViewModelNode* node = (ObjectDataViewModelNode*)parent.GetID();
    if (!node || node->GetChildCount() == 0)
        return;

    wxDataViewItemArray array;
    const size_t count = node->GetChildCount();
    for (size_t pos = 0; pos < count; pos++) {
        ObjectDataViewModelNode* child = node->GetChildren().Item(pos);
        array.Add(wxDataViewItem((void*)child));
        ItemAdded(parent, wxDataViewItem((void*)child));
    }

    for (const auto item : array)
        AddAllChildren(item);

    m_ctrl->Expand(parent);
};

wxDataViewItem ObjectDataViewModel::ReorganizeChildren( const int current_volume_id, 
                                                        const int new_volume_id, 
                                                        const wxDataViewItem &parent)
{
    auto ret_item = wxDataViewItem(0);
    if (current_volume_id == new_volume_id)
        return ret_item;
    wxASSERT(parent.IsOk());
    ObjectDataViewModelNode *node_parent = (ObjectDataViewModelNode*)parent.GetID();
    if (!node_parent)      // happens if item.IsOk()==false
        return ret_item;

    const size_t shift = node_parent->GetChildren().Item(0)->m_type == itSettings ? 1 : 0;

    ObjectDataViewModelNode *deleted_node = node_parent->GetNthChild(current_volume_id+shift);
    node_parent->GetChildren().Remove(deleted_node);
    ItemDeleted(parent, wxDataViewItem(deleted_node));
    node_parent->Insert(deleted_node, new_volume_id+shift);
    ItemAdded(parent, wxDataViewItem(deleted_node));

    // If some item has a children, just to add a deleted item is not enough on Linux 
    // We should to add all its children separately
    AddAllChildren(wxDataViewItem(deleted_node));

    //update volume_id value for child-nodes
    auto children = node_parent->GetChildren();
    int id_frst = current_volume_id < new_volume_id ? current_volume_id : new_volume_id;
    int id_last = current_volume_id > new_volume_id ? current_volume_id : new_volume_id;
    for (int id = id_frst; id <= id_last; ++id)
        children[id+shift]->SetIdx(id);

    return wxDataViewItem(node_parent->GetNthChild(new_volume_id+shift));
}

wxDataViewItem ObjectDataViewModel::ReorganizeObjects(  const int current_id, const int new_id)
{
    if (current_id == new_id)
        return wxDataViewItem(nullptr);

    ObjectDataViewModelNode* deleted_node = m_objects[current_id];
    m_objects.erase(m_objects.begin() + current_id);
    ItemDeleted(wxDataViewItem(nullptr), wxDataViewItem(deleted_node));

    m_objects.emplace(m_objects.begin() + new_id, deleted_node);
    ItemAdded(wxDataViewItem(nullptr), wxDataViewItem(deleted_node));

    // If some item has a children, just to add a deleted item is not enough on Linux 
    // We should to add all its children separately
    AddAllChildren(wxDataViewItem(deleted_node));

    return wxDataViewItem(deleted_node);
}

bool ObjectDataViewModel::IsEnabled(const wxDataViewItem &item, unsigned int col) const
{
    wxASSERT(item.IsOk());
    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();

    // disable extruder selection for the non "itObject|itVolume" item
    return !(col == colExtruder && node->m_extruder.IsEmpty());
}

wxDataViewItem ObjectDataViewModel::GetParent(const wxDataViewItem &item) const
{
	// the invisible root node has no parent
	if (!item.IsOk())
		return wxDataViewItem(0);

	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	assert(node != nullptr && node->valid());

	// objects nodes has no parent too
    if (node->m_type == itObject)
		return wxDataViewItem(0);

	return wxDataViewItem((void*)node->GetParent());
}

wxDataViewItem ObjectDataViewModel::GetTopParent(const wxDataViewItem &item) const
{
	// the invisible root node has no parent
	if (!item.IsOk())
		return wxDataViewItem(0);

	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
    if (node->m_type == itObject)
        return item;

    ObjectDataViewModelNode *parent_node = node->GetParent();
    while (parent_node->m_type != itObject)
        parent_node = parent_node->GetParent();

    return wxDataViewItem((void*)parent_node);
}

bool ObjectDataViewModel::IsContainer(const wxDataViewItem &item) const
{
	// the invisible root node can have children
	if (!item.IsOk())
		return true;

	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	return node->IsContainer();
}

unsigned int ObjectDataViewModel::GetChildren(const wxDataViewItem &parent, wxDataViewItemArray &array) const
{
	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)parent.GetID();
	if (!node)
	{
		for (auto object : m_objects)
			array.Add(wxDataViewItem((void*)object));
		return m_objects.size();
	}

	if (node->GetChildCount() == 0)
	{
		return 0;
	}

	unsigned int count = node->GetChildren().GetCount();
	for (unsigned int pos = 0; pos < count; pos++)
	{
		ObjectDataViewModelNode *child = node->GetChildren().Item(pos);
		array.Add(wxDataViewItem((void*)child));
	}

	return count;
}

void ObjectDataViewModel::GetAllChildren(const wxDataViewItem &parent, wxDataViewItemArray &array) const
{
	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)parent.GetID();
	if (!node) {
		for (auto object : m_objects)
			array.Add(wxDataViewItem((void*)object));
	}
	else if (node->GetChildCount() == 0)
		return;
    else {
        const size_t count = node->GetChildren().GetCount();
        for (size_t pos = 0; pos < count; pos++) {
            ObjectDataViewModelNode *child = node->GetChildren().Item(pos);
            array.Add(wxDataViewItem((void*)child));
        }
    }

    wxDataViewItemArray new_array = array;
    for (const auto item : new_array)
    {
        wxDataViewItemArray children;
        GetAllChildren(item, children);
        WX_APPEND_ARRAY(array, children);
    }
}

ItemType ObjectDataViewModel::GetItemType(const wxDataViewItem &item) const 
{
    if (!item.IsOk())
        return itUndef;
    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
    return node->m_type < 0 ? itUndef : node->m_type;
}

wxDataViewItem ObjectDataViewModel::GetItemByType(const wxDataViewItem &parent_item, ItemType type) const 
{
    if (!parent_item.IsOk())
        return wxDataViewItem(0);

    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)parent_item.GetID();
    if (node->GetChildCount() == 0)
        return wxDataViewItem(0);

    for (size_t i = 0; i < node->GetChildCount(); i++) {
        if (node->GetNthChild(i)->m_type == type)
            return wxDataViewItem((void*)node->GetNthChild(i));
    }

    return wxDataViewItem(0);
}

wxDataViewItem ObjectDataViewModel::GetSettingsItem(const wxDataViewItem &item) const
{
    return GetItemByType(item, itSettings);
}

wxDataViewItem ObjectDataViewModel::GetInstanceRootItem(const wxDataViewItem &item) const
{
    return GetItemByType(item, itInstanceRoot);
}

wxDataViewItem ObjectDataViewModel::GetLayerRootItem(const wxDataViewItem &item) const
{
    return GetItemByType(item, itLayerRoot);
}

bool ObjectDataViewModel::IsSettingsItem(const wxDataViewItem &item) const
{
    if (!item.IsOk())
        return false;
    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
    return node->m_type == itSettings;
}

void ObjectDataViewModel::UpdateSettingsDigest(const wxDataViewItem &item, 
                                                    const std::vector<std::string>& categories)
{
    if (!item.IsOk()) return;
    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
    if (!node->update_settings_digest(categories))
        return;
    ItemChanged(item);
}

void ObjectDataViewModel::SetVolumeType(const wxDataViewItem &item, const Slic3r::ModelVolumeType type)
{
    if (!item.IsOk() || GetItemType(item) != itVolume) 
        return;

    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
    node->SetBitmap(*m_volume_bmps[int(type)]);
    ItemChanged(item);
}

wxDataViewItem ObjectDataViewModel::SetPrintableState(
    PrintIndicator  printable,
    int             obj_idx,
    int             subobj_idx /* = -1*/,
    ItemType        subobj_type/* = itInstance*/)
{
    wxDataViewItem item = wxDataViewItem(0);
    if (subobj_idx < 0)
        item = GetItemById(obj_idx);
    else
        item =  subobj_type&itInstance ? GetItemByInstanceId(obj_idx, subobj_idx) :
                GetItemByVolumeId(obj_idx, subobj_idx);

    ObjectDataViewModelNode* node = (ObjectDataViewModelNode*)item.GetID();
    if (!node)
        return wxDataViewItem(0);
    node->set_printable_icon(printable);
    ItemChanged(item);

    if (subobj_idx >= 0)
        UpdateObjectPrintable(GetItemById(obj_idx));

    return item;
}

wxDataViewItem ObjectDataViewModel::SetObjectPrintableState(
    PrintIndicator  printable,
    wxDataViewItem  obj_item)
{
    ObjectDataViewModelNode* node = (ObjectDataViewModelNode*)obj_item.GetID();
    if (!node)
        return wxDataViewItem(0);
    node->set_printable_icon(printable);
    ItemChanged(obj_item);

    UpdateInstancesPrintable(obj_item);

    return obj_item;
}

void ObjectDataViewModel::Rescale()
{
    wxDataViewItemArray all_items;
    GetAllChildren(wxDataViewItem(0), all_items);

    for (wxDataViewItem item : all_items)
    {
        if (!item.IsOk())
            continue;

        ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
        node->msw_rescale();

        switch (node->m_type)
        {
        case itObject:
            if (node->m_bmp.IsOk()) node->m_bmp = *m_warning_bmp;
            break;
        case itVolume:
            node->m_bmp = GetVolumeIcon(node->m_volume_type, node->m_bmp.GetWidth() != node->m_bmp.GetHeight());
            break;
        case itLayerRoot:
            node->m_bmp = create_scaled_bitmap(LAYER_ROOT_ICON);
        case itLayer:
            node->m_bmp = create_scaled_bitmap(LAYER_ICON);
            break;
        default: break;
        }

        ItemChanged(item);
    }
}

wxBitmap ObjectDataViewModel::GetVolumeIcon(const Slic3r::ModelVolumeType vol_type, const bool is_marked/* = false*/)
{
    if (!is_marked)
        return *m_volume_bmps[static_cast<int>(vol_type)];

    std::string scaled_bitmap_name = "warning" + std::to_string(static_cast<int>(vol_type));
    scaled_bitmap_name += "-em" + std::to_string(Slic3r::GUI::wxGetApp().em_unit());

    wxBitmap *bmp = m_bitmap_cache->find(scaled_bitmap_name);
    if (bmp == nullptr) {
        std::vector<wxBitmap> bmps;

        bmps.emplace_back(*m_warning_bmp);
        bmps.emplace_back(*m_volume_bmps[static_cast<int>(vol_type)]);

        bmp = m_bitmap_cache->insert(scaled_bitmap_name, bmps);
    }

    return *bmp;
}

void ObjectDataViewModel::DeleteWarningIcon(const wxDataViewItem& item, const bool unmark_object/* = false*/)
{
    if (!item.IsOk())
        return;

    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();

    if (!node->GetBitmap().IsOk() || !(node->GetType() & (itVolume | itObject)))
        return;

    if (node->GetType() & itVolume) {
        node->SetBitmap(*m_volume_bmps[static_cast<int>(node->volume_type())]);
        return;
    }

    node->SetBitmap(wxNullBitmap);
    if (unmark_object)
    {
        wxDataViewItemArray children;
        GetChildren(item, children);
        for (const wxDataViewItem& child : children)
            DeleteWarningIcon(child);
    }
}

} // namespace GUI
} // namespace Slic3r


