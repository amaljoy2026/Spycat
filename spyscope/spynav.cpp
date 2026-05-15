// SpyNav.cpp
#include "spynav.hpp"
#include "search_icon.h"
#include "type_icons.h"

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

// ── Type tag icon ─────────────────────────────────────────────────────────────

static int TypeTagIcon(TypeTag tag)
{
    switch (tag) {
        case TypeTag::Double: return NAV_ICON_DOUBLE;
        case TypeTag::Float:  return NAV_ICON_FLOAT;
        case TypeTag::Int64:  return NAV_ICON_INT64;
        case TypeTag::Int32:  return NAV_ICON_INT32;
        case TypeTag::Bool:   return NAV_ICON_BOOL;
        case TypeTag::String: return NAV_ICON_STRING;
        default:              return NAV_ICON_RAW;
    }
}

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
{
    app_.RegisterObserver(this);

    SetBackgroundColour(app_.GetTheme().GetPrimaryBackgroundColor());

    tree_ = new wxTreeCtrl(this, wxID_ANY,
                           wxDefaultPosition, wxDefaultSize,
                           wxTR_DEFAULT_STYLE |
                           wxTR_HIDE_ROOT |
                           wxTR_FULL_ROW_HIGHLIGHT |
                           wxTR_NO_LINES |
                           wxTR_MULTIPLE |
                           wxBORDER_NONE);

    tree_->SetBackgroundColour(app_.GetTheme().GetPrimaryBackgroundColor());
    tree_->SetForegroundColour(app_.GetTheme().GetPrimaryTextColor());
    tree_->SetFont(app_.GetTheme().GetFont());

    // ── Image list ────────────────────────────────────────────────────────
    // One 16×16 icon per NavIcon slot — order must match the enum in spynav.hpp
    struct IconEntry { const uint8_t* data; size_t size; };
    static const IconEntry kIcons[NAV_ICON_COUNT] = {
        { kTypeIcon_NamespacePng, kTypeIcon_NamespacePngSize },
        { kTypeIcon_DoublePng,    kTypeIcon_DoublePngSize    },
        { kTypeIcon_FloatPng,     kTypeIcon_FloatPngSize     },
        { kTypeIcon_Int32Png,     kTypeIcon_Int32PngSize     },
        { kTypeIcon_Int64Png,     kTypeIcon_Int64PngSize     },
        { kTypeIcon_BoolPng,      kTypeIcon_BoolPngSize      },
        { kTypeIcon_StringPng,    kTypeIcon_StringPngSize    },
        { kTypeIcon_RawPng,       kTypeIcon_RawPngSize       },
    };

    image_list_ = new wxImageList(22, 22, true, NAV_ICON_COUNT);
    for (const auto& ic : kIcons) {
        wxMemoryInputStream stream(ic.data, ic.size);
        wxImage img(stream, wxBITMAP_TYPE_PNG);
        img.Rescale(22, 22, wxIMAGE_QUALITY_NEAREST);
        image_list_->Add(wxBitmap(img));
    }
    tree_->AssignImageList(image_list_);  // tree takes ownership
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

// ── Destructor ────────────────────────────────────────────────────────────────

SpyNav::~SpyNav()
{
    app_.UnregisterObserver(this);
}

// ── Poll / data timer ─────────────────────────────────────────────────────────

void SpyNav::OnDataPoll()
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
                                          NAV_ICON_NAMESPACE, NAV_ICON_NAMESPACE);
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

    int icon = -1;
    DataSource* source = app_.GetDataSource();
    if (source) {
        auto entry = source->Get(key);
        if (entry) icon = TypeTagIcon(entry->type_tag);
    }

    wxTreeItemId leaf = tree_->AppendItem(parent, leaf_name, icon, icon);

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

// Recursively walk the visible tree from `item`, appending every leaf key to `out`.
// Namespace nodes (no TreeData) are descended into; leaves are collected directly.
void SpyNav::CollectLeafKeys(wxTreeItemId item, wxArrayString& out) const
{
    auto* d = dynamic_cast<TreeData*>(tree_->GetItemData(item));
    if (d) {
        // Leaf node — add key and stop
        out.Add(d->GetKey());
        return;
    }

    // Namespace node — recurse into all visible children
    wxTreeItemIdValue cookie;
    wxTreeItemId child = tree_->GetFirstChild(item, cookie);
    while (child.IsOk()) {
        CollectLeafKeys(child, out);
        child = tree_->GetNextChild(item, cookie);
    }
}

void SpyNav::OnBeginDrag(wxTreeEvent& e)
{
    wxTreeItemId dragged = e.GetItem();
    if (!dragged.IsOk()) return;

    // Gather all currently selected items
    wxArrayTreeItemIds selections;
    tree_->GetSelections(selections);

    // If the dragged item isn't in the selection, treat it as a solo drag
    bool in_selection = false;
    for (const auto& sel : selections)
        if (sel == dragged) { in_selection = true; break; }

    // Collect keys, de-duplicating via an unordered_set
    std::unordered_set<std::string> seen;
    wxArrayString keys;

    auto add_item = [&](wxTreeItemId item) {
        wxArrayString item_keys;
        CollectLeafKeys(item, item_keys);
        for (const auto& k : item_keys) {
            std::string ks = k.ToStdString();
            if (seen.insert(ks).second)
                keys.Add(k);
        }
    };

    if (in_selection && selections.size() > 1) {
        for (const auto& sel : selections)
            add_item(sel);
    } else {
        add_item(dragged);
    }

    if (keys.empty()) return;   // nothing draggable selected

    // Do NOT call e.Allow() — that starts the tree's own native drag state
    // machine, which conflicts with our manual DoDragDrop and leaves the tree
    // in a half-captured state after the drop completes, eating subsequent
    // mouse events.

    wxString drag_text = wxJoin(keys, '\n');
    wxTextDataObject drag_data(drag_text);
    wxDropSource source(drag_data, tree_);
    source.DoDragDrop(wxDrag_CopyOnly);

    if (tree_->HasCapture())
        tree_->ReleaseMouse();
    if (this->HasCapture())
        this->ReleaseMouse();
}

void SpyNav::OnSearchText(wxCommandEvent&)   { ApplySearch(); }
void SpyNav::OnSearchButton(wxCommandEvent&) { ApplySearch(); }

void SpyNav::ApplySearch()
{
    // Build lowercase query
    std::string query = search_ctrl_->GetValue().ToStdString();
    std::transform(query.begin(), query.end(), query.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    // Rebuild the visible tree from known_cache_ in sorted order
    tree_->DeleteAllItems();
    node_cache_.clear();
    root_ = tree_->AddRoot("__root__");

    // Sort keys so the tree is stable and predictable regardless of hash order
    std::vector<std::string> sorted_keys(known_cache_.begin(), known_cache_.end());
    std::sort(sorted_keys.begin(), sorted_keys.end());

    for (const auto& key : sorted_keys) {
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

    // If the right-clicked item isn't already selected, select it exclusively
    if (!tree_->IsSelected(item))
        tree_->SelectItem(item);

    // Collect all selected leaf keys
    wxArrayTreeItemIds selections;
    tree_->GetSelections(selections);

    std::vector<std::string> keys;
    for (const auto& sel : selections) {
        auto* d = dynamic_cast<TreeData*>(tree_->GetItemData(sel));
        if (d) keys.push_back(d->GetKey().ToStdString());
    }

    if (keys.empty()) return;  // only namespace nodes selected

    const size_t n = keys.size();
    wxMenu menu;
    menu.Append(ID_NAV_PLOT,  n == 1 ? "Plot"
                                     : wxString::Format("Plot (%zu keys)", n));
    menu.Append(ID_NAV_WATCH, n == 1 ? "Watch"
                                     : wxString::Format("Watch (%zu keys)", n));

    menu.Bind(wxEVT_MENU, [this, keys](wxCommandEvent&) {
        app_.AddPlotPane(keys);
    }, ID_NAV_PLOT);

    menu.Bind(wxEVT_MENU, [this, keys](wxCommandEvent&) {
        app_.AddWatchPane(keys);
    }, ID_NAV_WATCH);

    tree_->PopupMenu(&menu);
}

} // namespace spycat
