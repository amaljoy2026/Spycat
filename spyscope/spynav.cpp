// SpyNav.cpp
#include "spynav.hpp"
#include "namespace_icon.h"
#include "search_icon.h"

#include <wx/sizer.h>
#include <wx/imaglist.h>
#include <wx/dnd.h>
#include <wx/mstream.h>
#include <wx/bmpbuttn.h>

namespace spycat
{

// Context-menu command IDs
enum
{
    ID_NAV_PLOT  = wxID_HIGHEST + 101,
    ID_NAV_WATCH = wxID_HIGHEST + 102,
};

wxBEGIN_EVENT_TABLE(SpyNav, wxPanel)
    EVT_TREE_SEL_CHANGED    (wxID_ANY, SpyNav::OnSelChanged)
    EVT_TREE_BEGIN_DRAG     (wxID_ANY, SpyNav::OnBeginDrag)
    EVT_TREE_ITEM_EXPANDING (wxID_ANY, SpyNav::OnItemExpanding)
    EVT_TREE_ITEM_RIGHT_CLICK(wxID_ANY, SpyNav::OnItemRightClick)
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

    // ── Image list ────────────────────────────────────────────────────────
    image_list_ = new wxImageList(16, 16, true, NAV_ICON_COUNT);

    // NAV_ICON_NAMESPACE — load embedded PNG, scale to 16×16
    {
        wxMemoryInputStream stream(kNamespaceIconPng, kNamespaceIconPngSize);
        wxImage img(stream, wxBITMAP_TYPE_PNG);
        img.Rescale(16, 16, wxIMAGE_QUALITY_HIGH);
        image_list_->Add(wxBitmap(img));
    }

    tree_->AssignImageList(image_list_);   // tree takes ownership
    image_list_ = nullptr;

    // Hidden root
    root_ = tree_->AddRoot("__root__");

    // ── Search bar ────────────────────────────────────────────────────────
    search_ctrl_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                                  wxDefaultPosition, wxDefaultSize,
                                  wxTE_PROCESS_ENTER | wxBORDER_SIMPLE);
    search_ctrl_->SetBackgroundColour(app_.GetTheme().GetPrimaryBackgroundColor());
    search_ctrl_->SetForegroundColour(app_.GetTheme().GetPrimaryTextColor());
    search_ctrl_->SetFont(app_.GetTheme().GetFont());
    search_ctrl_->SetHint("Search...");

    // Load search icon from embedded PNG
    {
        wxMemoryInputStream stream(kSearchIconPng, kSearchIconPngSize);
        wxImage img(stream, wxBITMAP_TYPE_PNG);
        search_btn_ = new wxBitmapButton(this, wxID_ANY, wxBitmap(img),
                                         wxDefaultPosition, wxSize(28, 28),
                                         wxBORDER_NONE);
    }
    search_btn_->SetBackgroundColour(app_.GetTheme().GetPrimaryBackgroundColor());
    search_btn_->SetToolTip("Search");

    search_ctrl_->Bind(wxEVT_TEXT,       &SpyNav::OnSearchText,   this);
    search_ctrl_->Bind(wxEVT_TEXT_ENTER, &SpyNav::OnSearchButton, this);
    search_btn_->Bind (wxEVT_BUTTON,     &SpyNav::OnSearchButton, this);

    auto* search_row = new wxBoxSizer(wxHORIZONTAL);
    search_row->Add(search_ctrl_, 1, wxALIGN_CENTER_VERTICAL | wxALL, 2);
    search_row->Add(search_btn_,  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(search_row, 0, wxEXPAND);
    sizer->Add(tree_,      1, wxEXPAND);
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

    wxTreeItemId leaf = tree_->AppendItem(parent, leaf_name, -1, -1);

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

void SpyNav::OnSearchText(wxCommandEvent&)   { ApplySearch(); }
void SpyNav::OnSearchButton(wxCommandEvent&) { ApplySearch(); }

void SpyNav::ApplySearch()
{
    // Build lowercase query
    std::string query = search_ctrl_->GetValue().ToStdString();
    std::transform(query.begin(), query.end(), query.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    // Rebuild the visible tree from known_cache_
    tree_->DeleteAllItems();
    node_cache_.clear();
    root_ = tree_->AddRoot("__root__");

    for (const auto& key : known_cache_) {
        if (!query.empty()) {
            // Case-insensitive substring match against full dotted key
            std::string lower_key = key;
            std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            if (lower_key.find(query) == std::string::npos)
                continue;
        }
        InsertKey(key);
    }
}

void SpyNav::OnItemRightClick(wxTreeEvent& e)
{
    wxTreeItemId item = e.GetItem();
    if (!item.IsOk()) return;

    auto* data = dynamic_cast<TreeData*>(tree_->GetItemData(item));
    if (!data) return;  // namespace node — no menu

    // Visually select the right-clicked item
    tree_->SelectItem(item);

    const std::string key = data->GetKey().ToStdString();

    wxMenu menu;
    menu.Append(ID_NAV_PLOT,  "Plot");
    menu.Append(ID_NAV_WATCH, "Watch");

    menu.Bind(wxEVT_MENU, [this, key](wxCommandEvent&) {
        app_.AddPlotPane(key);
    }, ID_NAV_PLOT);

    menu.Bind(wxEVT_MENU, [this, key](wxCommandEvent&) {
        app_.AddWatchPane(key);
    }, ID_NAV_WATCH);

    tree_->PopupMenu(&menu);
}

} // namespace spycat
