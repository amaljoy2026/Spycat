// SpyNav.cpp
#include "spynav.hpp"

#include <wx/sizer.h>
#include <wx/imaglist.h>
#include <wx/dnd.h>

namespace spycat
{

wxBEGIN_EVENT_TABLE(SpyNav, wxPanel)
    EVT_TREE_SEL_CHANGED(wxID_ANY, SpyNav::OnSelChanged)
    EVT_TREE_BEGIN_DRAG (wxID_ANY, SpyNav::OnBeginDrag)
    EVT_TREE_ITEM_EXPANDING(wxID_ANY, SpyNav::OnItemExpanding)
wxEND_EVENT_TABLE()

// ── Construction ──────────────────────────────────────────────────────────────

SpyNav::SpyNav(wxWindow* parent, App& app, wxWindowID id)
    : wxPanel(parent, id)
    , app_(app)
    , data_timer_(this)
{
    Bind(wxEVT_TIMER, &SpyNav::OnDataTimer, this, data_timer_.GetId());
    data_timer_.Start(17);   // ~60 Hz

    SetBackgroundColour(app_.GetTheme().GetPrimaryBackgroundColor());

    tree_ = new wxTreeCtrl(this, wxID_ANY,
                           wxDefaultPosition, wxDefaultSize,
                           wxTR_DEFAULT_STYLE | 
                           wxTR_HIDE_ROOT | 
                           wxTR_FULL_ROW_HIGHLIGHT |
                           wxTR_NO_LINES |
                           wxBORDER_NONE);

    tree_->SetBackgroundColour(app_.GetTheme().GetPrimaryBackgroundColor());
    tree_->SetForegroundColour(app_.GetTheme().GetPrimaryTextColor());
    tree_->SetFont(app_.GetTheme().GetFont());

    // ── Image list — placeholder bitmaps, swap for real assets later ──────
    image_list_ = new wxImageList(16, 16, true, NAV_ICON_COUNT);

    // NAV_ICON_NAMESPACE — filled green square (folder placeholder)
    {
        wxBitmap bmp(16, 16);
        wxMemoryDC dc(bmp);
        dc.SetBackground(wxBrush(app_.GetTheme().GetPrimaryBackgroundColor()));
        dc.Clear();
        dc.SetBrush(wxBrush(app_.GetTheme().GetHighlightColor()));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(2, 4, 12, 9);   // simple folder-ish rectangle
        dc.SelectObject(wxNullBitmap);
        image_list_->Add(bmp);
    }

    // NAV_ICON_LEAF — small green circle (signal placeholder)
    {
        wxBitmap bmp(16, 16);
        wxMemoryDC dc(bmp);
        dc.SetBackground(wxBrush(app_.GetTheme().GetPrimaryBackgroundColor()));
        dc.Clear();
        dc.SetBrush(wxBrush(app_.GetTheme().GetHighlightColor()));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawCircle(8, 8, 4);
        dc.SelectObject(wxNullBitmap);
        image_list_->Add(bmp);
    }

    tree_->AssignImageList(image_list_);   // tree takes ownership
    image_list_ = nullptr;
    
    // Hidden root
    root_ = tree_->AddRoot("__root__");

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(tree_, 1, wxEXPAND);
    SetSizer(sizer);
}

// ── Poll / data timer ─────────────────────────────────────────────────────────

void SpyNav::OnDataTimer(wxTimerEvent&)
{
    Poll();
}

void SpyNav::Poll()
{
    DataSource *source = app_.GetDataSource();
    
    if (!source || !source->IsReady()) return;

    std::vector<std::string> keys = source->GetKeys();

    // Only act on keys we haven't seen before — everything else is a no-op
    for (const auto& key : keys) {
        if (known_cache_.count(key) == 0) {
            known_cache_.insert(key);
            InsertKey(key);
        }
    }
}

// ── Tree building ─────────────────────────────────────────────────────────────

// Returns the tree item for a dotted path, creating intermediate nodes as needed.
// e.g. "Engine.Cylinder" → creates Engine → Cylinder, returns Cylinder's item.
wxTreeItemId SpyNav::GetOrCreateNode(const wxString& path)
{
    auto it = node_cache_.find(path.ToStdString());
    if (it != node_cache_.end())
        return it->second;

    int dot = path.Find('.', /*fromEnd=*/true);

    wxTreeItemId parent;
    wxString     label;

    if (dot == wxNOT_FOUND) {
        // Top-level bucket — parent is hidden root
        parent = root_;
        label  = path;
    } else {
        wxString parent_path = path.Left(dot);
        label  = path.Mid(dot + 1);
        parent = GetOrCreateNode(parent_path);  // recurse to ensure parent exists
    }

    wxTreeItemId node = tree_->AppendItem(parent, label,
                                          NAV_ICON_NAMESPACE,
                                          NAV_ICON_NAMESPACE);
    node_cache_[path.ToStdString()] = node;
    return node;
}

void SpyNav::InsertKey(const std::string& key)
{
    wxString wx_key = wxString::FromUTF8(key);

    int dot = wx_key.Find('.', /*fromEnd=*/true);

    wxTreeItemId parent;
    wxString     leaf_name;

    if (dot == wxNOT_FOUND) {
        // No namespace — goes under Global bucket
        parent    = root_;
        leaf_name = wx_key;
    } else {
        wxString ns_path = wx_key.BeforeLast('.');
        leaf_name        = wx_key.AfterLast('.');
        parent           = GetOrCreateNode(ns_path);
    }

    wxTreeItemId leaf = tree_->AppendItem(parent, leaf_name,
                                          NAV_ICON_LEAF,
                                          NAV_ICON_LEAF);

    // Store the full dotted key in item data for drag and selection
    tree_->SetItemData(leaf, new TreeData(wx_key));

    // Expand the immediate parent so new keys are visible
    tree_->Expand(parent);
}

// ── Events ────────────────────────────────────────────────────────────────────

void SpyNav::OnSelChanged(wxTreeEvent& e)
{
    wxTreeItemId item = e.GetItem();
    if (!item.IsOk()) return;

    auto* data = dynamic_cast<TreeData*>(tree_->GetItemData(item));
    if (!data) {
        // Namespace node selected — clear selection key
        selected_key_.clear();
        return;
    }

    selected_key_ = data->GetKey().ToStdString();

    if (OnKeySelected)
        OnKeySelected(selected_key_);

    e.Skip();
}

void SpyNav::OnBeginDrag(wxTreeEvent& e)
{
    e.Skip();

    wxTreeItemId item = e.GetItem();
    if (!item.IsOk()) return;

    auto* data = dynamic_cast<TreeData*>(tree_->GetItemData(item));
    if (!data) {
        // Namespace node — don't allow drag
        return;
    }
    
    // Allow the drag to proceed
    e.Allow();

    wxString full_key = data->GetKey();
    wxTextDataObject drag_data(full_key);
    wxDropSource source(drag_data, tree_);
    wxDragResult result = source.DoDragDrop(wxDrag_CopyOnly);
    
    // Explicitly release internal focus states immediately after execution
    if (this->HasCapture()) {
        this->ReleaseMouse();
    }
}

} // namespace spycat
