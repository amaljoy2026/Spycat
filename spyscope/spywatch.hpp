// spywatch.hpp
#ifndef __SPYSCOPE_WATCH_HPP__
#define __SPYSCOPE_WATCH_HPP__

#include <wx/wx.h>
#include <wx/scrolwin.h>
#include <wx/gbsizer.h>
#include <string>
#include <vector>
#include <functional>

#include "datasource.hpp"
#include "app.hpp"

// Forward-declare SpyScope (global namespace) — full definition in app.hpp,
// included only in spywatch.cpp to avoid a circular header dependency.
class SpyScope;

namespace spycat
{

// Forward-declare DataSource — full definition included in spywatch.cpp
class DataSource;

// ── Colours ───────────────────────────────────────────────────────────────────
static const wxColour WATCH_BG        { 0xFF, 0xFF, 0xFF };
static const wxColour WATCH_PRIMARY   { 0x00, 0x66, 0x00 };
static const wxColour WATCH_ROW_ALT   { 0xF0, 0xF7, 0xF0 };
static const wxColour WATCH_HEADER_BG { 0x00, 0x66, 0x00 };
static const wxColour WATCH_HEADER_FG { 0xFF, 0xFF, 0xFF };
static const wxColour WATCH_TEXT      { 0x00, 0x00, 0x00 };
static const wxColour WATCH_VALUE_OVR { 0x00, 0x66, 0x00 };
static const wxColour WATCH_FIELD_BG  { 0xF5, 0xF5, 0xF5 };

// ── ToggleBox — custom drawn checkbox ────────────────────────────────────────
class ToggleBox : public wxPanel
{
public:
    ToggleBox(wxWindow* parent, wxColour bg,
              std::function<void(bool)> on_change);

    bool IsChecked() const { return checked_; }
    void SetChecked(bool v) { checked_ = v; Refresh(); }

private:
    void OnPaint(wxPaintEvent&);
    void OnClick(wxMouseEvent&);

    bool                      checked_ = false;
    wxColour                  bg_;
    std::function<void(bool)> on_change_;
};

// ── WatchEntry ────────────────────────────────────────────────────────────────
struct WatchEntry
{
    std::string key;
    std::string type;
    std::string value;

    bool        override_active   = false;
    std::string override_value;
    int32_t     override_priority = 1;
};

// ── SpyWatch ──────────────────────────────────────────────────────────────────
class SpyWatch : public wxScrolledWindow
{
public:
    SpyWatch(wxWindow* parent, App& app, wxWindowID id = wxID_ANY);

    // Refresh all watched keys from DataSource
    void Poll();

    void AddKey(const std::string& key);
    void RemoveKey(const std::string& key);
    void UpdateEntry(const std::string& key,
                     const std::string& type,
                     const std::string& value);
    std::vector<WatchEntry> GetOverrides() const;
    void Clear();

private:
    void OnDataTimer(wxTimerEvent&);
    void RebuildRows();
    void OnSize(wxSizeEvent&);

    struct RowWidgets
    {
        wxStaticText* key_label   = nullptr;
        wxStaticText* type_label  = nullptr;
        wxStaticText* value_label = nullptr;
        ToggleBox*    ovr_toggle  = nullptr;
        wxTextCtrl*   ovr_field   = nullptr;
    };

    void       BuildHeaderRow(wxGridBagSizer* sizer);
    RowWidgets BuildDataRow(wxGridBagSizer* sizer, int row, WatchEntry& entry);

    void OnOverrideToggle(bool checked, size_t index);
    void OnOverrideText(size_t index);

    static constexpr int COL_KEY   = 0;
    static constexpr int COL_TYPE  = 1;
    static constexpr int COL_VALUE = 2;
    static constexpr int COL_OVR   = 3;
    static constexpr int ROW_H     = 28;
    static constexpr int HEADER_H  = 32;

    App&                     app_;
    wxTimer                  data_timer_;

    std::vector<WatchEntry>  entries_;
    std::vector<RowWidgets>  row_widgets_;
    wxPanel*                 inner_  = nullptr;
    wxGridBagSizer*          sizer_  = nullptr;
    wxFont                   font_mono_;
    wxFont                   font_header_;
};

} // namespace spycat

#endif // __SPYSCOPE_WATCH_HPP__
