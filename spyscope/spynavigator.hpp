// spynavigator.hpp
#ifndef __SPYSCOPE_NAVIGATOR_HPP__
#define __SPYSCOPE_NAVIGATOR_HPP__

#include <wx/wx.h>
#include <wx/treectrl.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>

// Forward-declare SpyScope (global namespace) — full definition in app.hpp,
// included only in spynavigator.cpp to avoid a circular header dependency.
class SpyScope;

namespace spycat
{

// Forward-declare DataSource — full definition included in spynavigator.cpp
class DataSource;

static const wxColour NAV_BG        { 0xFF, 0xFF, 0xFF };
static const wxColour NAV_PRIMARY   { 0x00, 0x66, 0x00 };
static const wxColour NAV_SEL_BG    { 0x00, 0x66, 0x00 };
static const wxColour NAV_SEL_FG    { 0xFF, 0xFF, 0xFF };
static const wxColour NAV_TEXT      { 0x00, 0x00, 0x00 };

static const wxString NAV_GLOBAL_BUCKET = "Global";

// Icon indices — assign assets here when ready
enum NavIcon
{
    NAV_ICON_NAMESPACE = 0,
    NAV_ICON_LEAF      = 1,
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

// ── SpyNavigator ──────────────────────────────────────────────────────────────

class SpyNavigator : public wxPanel
{
public:
    SpyNavigator(wxWindow* parent, SpyScope& app, wxWindowID id = wxID_ANY);

    void Poll();

    // Currently selected full key — empty if a namespace node is selected
    std::string GetSelectedKey() const { return selected_key_; }

    // Optional callback fired when a leaf key is selected
    std::function<void(const std::string& key)> OnKeySelected;

private:
    // Tree building
    wxTreeItemId GetOrCreateNode(const wxString& path);
    void         InsertKey(const std::string& key);

    // Events
    void OnSelChanged(wxTreeEvent&);
    void OnBeginDrag(wxTreeEvent&);
    void OnItemExpanding(wxTreeEvent& e) { e.Skip(); }
    void OnDataTimer(wxTimerEvent&);

    wxTreeCtrl*   tree_;
    DataSource*   source_;
    wxTimer       data_timer_;

    // Hidden root — wxTR_HIDE_ROOT makes its children appear top-level
    wxTreeItemId  root_;

    // Maps dotted namespace path → tree item (e.g. "Engine.Cylinder" → item)
    std::unordered_map<std::string, wxTreeItemId> node_cache_;

    std::unordered_set<std::string> known_cache_;

    std::string selected_key_;

    wxImageList* image_list_ = nullptr;

    wxFont font_mono_;

    wxDECLARE_EVENT_TABLE();
};

} // namespace spycat

#endif // __SPYSCOPE_NAVIGATOR_HPP__
