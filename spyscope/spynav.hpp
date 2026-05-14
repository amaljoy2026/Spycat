// SpyNav.hpp
#ifndef __SPYSCOPE_SPYNAV_HPP__
#define __SPYSCOPE_SPYNAV_HPP__

#include <wx/wx.h>
#include <wx/treectrl.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <algorithm>
#include <cctype>

#include "datasource.hpp"
#include "app.hpp"

// Forward-declare SpyScope (global namespace) — full definition in app.hpp,
// included only in SpyNav.cpp to avoid a circular header dependency.
class SpyScope;

namespace spycat
{

// Forward-declare DataSource — full definition included in SpyNav.cpp
class DataSource;

// Icon indices — assign assets here when ready
enum NavIcon
{
    NAV_ICON_NAMESPACE = 0,
    NAV_ICON_COUNT
};

// ── TreeData — stores the full dotted key on each leaf item ──────────────────
class TreeData : public wxTreeItemData
{
public:
    TreeData(const wxString& key) : key_(key) {}
    const wxString& GetKey() const { return key_; }
private:
    wxString key_;
};

// ── SpyNav ──────────────────────────────────────────────────────────────

class SpyNav : public wxPanel, public DataObserver
{
public:
    SpyNav(wxWindow* parent, App& app, wxWindowID id = wxID_ANY);
    ~SpyNav() override;

    void Poll();
    void OnDataPoll() override;

    // Currently selected full key — empty if a namespace node is selected
    std::string GetSelectedKey() const { return selected_key_; }

    // Optional callback fired when a leaf key is selected
    std::function<void(const std::string& key)> OnKeySelected;

private:
    // Tree building
    wxTreeItemId GetOrCreateNode(const wxString& path);
    void         InsertKey(const std::string& key);

    // Drag helpers
    // Recursively collect all visible leaf keys under a tree item into out.
    void CollectLeafKeys(wxTreeItemId item, wxArrayString& out) const;

    // Search
    void ApplySearch();

    // Events
    void OnSelChanged(wxTreeEvent&);
    void OnBeginDrag(wxTreeEvent&);
    void OnItemExpanding(wxTreeEvent& e) { e.Skip(); }
    void OnItemRightClick(wxTreeEvent&);
    void OnSearchText(wxCommandEvent&);
    void OnSearchButton(wxCommandEvent&);

    App&          app_;

    wxTreeCtrl*   tree_;

    // Hidden root — wxTR_HIDE_ROOT makes its children appear top-level
    wxTreeItemId  root_;

    // Maps dotted namespace path → tree item (e.g. "Engine.Cylinder" → item)
    std::unordered_map<std::string, wxTreeItemId> node_cache_;

    std::unordered_set<std::string> known_cache_;

    std::string selected_key_;

    wxImageList* image_list_ = nullptr;

    wxFont        font_mono_;
    wxTextCtrl*   search_ctrl_ = nullptr;
    wxBitmapButton* search_btn_ = nullptr;

    wxDECLARE_EVENT_TABLE();
};

} // namespace spycat

#endif // __SPYSCOPE_SPYNAV_HPP__
