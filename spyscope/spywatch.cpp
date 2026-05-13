// spywatch.cpp
#include "spywatch.hpp"
#include "app.hpp"
#include "datasource.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <variant>

namespace spycat
{

// ── Formatting helpers ────────────────────────────────────────────────────────

static std::string type_tag_to_string(TypeTag tag)
{
    switch (tag) {
        case TypeTag::Double: return "f64";
        case TypeTag::Float:  return "f32";
        case TypeTag::Int64:  return "i64";
        case TypeTag::Int32:  return "i32";
        case TypeTag::Bool:   return "bool";
        case TypeTag::String: return "string";
        default:              return "raw";
    }
}

static std::string entry_value_to_string(const Spymap::Entry& e)
{
    return std::visit([](auto&& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>) {
            return v ? "true" : "false";
        } else if constexpr (std::is_same_v<T, double>) {
            std::ostringstream ss;
            ss << std::setprecision(7) << v;
            return ss.str();
        } else if constexpr (std::is_same_v<T, float>) {
            std::ostringstream ss;
            ss << std::setprecision(5) << v;
            return ss.str();
        } else if constexpr (std::is_arithmetic_v<T>) {
            return std::to_string(v);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return v;
        } else {
            return "[" + std::to_string(v.size()) + " bytes]";
        }
    }, e.value);
}

// ── ToggleBox ─────────────────────────────────────────────────────────────────

ToggleBox::ToggleBox(wxWindow* parent, wxColour bg,
                     std::function<void(bool)> on_change)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(16, 16))
    , bg_(bg)
    , on_change_(on_change)
{
    SetBackgroundColour(bg);
    SetMinSize(wxSize(16, 16));
    SetMaxSize(wxSize(16, 16));
    Bind(wxEVT_PAINT,     &ToggleBox::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, &ToggleBox::OnClick, this);
    Bind(wxEVT_LEFT_DCLICK, &ToggleBox::OnClick, this);
}

void ToggleBox::OnClick(wxMouseEvent&)
{
    checked_ = !checked_;
    Refresh();
    if (on_change_) on_change_(checked_);
}

void ToggleBox::OnPaint(wxPaintEvent&)
{
    wxPaintDC dc(this);

    // Fill parent bg first to avoid artefacts
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(bg_));
    dc.DrawRectangle(0, 0, 16, 16);

    // Box — green border, light grey fill
    dc.SetPen(wxPen(WATCH_PRIMARY, 1));
    dc.SetBrush(wxBrush(WATCH_FIELD_BG));
    dc.DrawRectangle(1, 1, 14, 14);

    // Tick mark
    if (checked_) {
        dc.SetPen(wxPen(WATCH_PRIMARY, 2));
        dc.DrawLine(3,  8,  6, 12);
        dc.DrawLine(6, 12, 13,  4);
    }
}

// ── SpyWatch ──────────────────────────────────────────────────────────────────

SpyWatch::SpyWatch(wxWindow* parent, SpyScope& app, wxWindowID id)
    : wxScrolledWindow(parent, id)
    , source_(app.GetDataSource())
    , data_timer_(this)
{
    Bind(wxEVT_TIMER, &SpyWatch::OnDataTimer, this, data_timer_.GetId());
    data_timer_.Start(17);   // ~60 Hz
    font_mono_   = wxFont(14, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    font_header_ = wxFont(14, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);

    SetBackgroundColour(WATCH_BG);
    SetScrollRate(0, ROW_H);

    inner_ = new wxPanel(this, wxID_ANY);
    inner_->SetBackgroundColour(WATCH_BG);

    RebuildRows();

    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(inner_, 0, wxEXPAND);
    SetSizer(outer);

    Bind(wxEVT_SIZE, &SpyWatch::OnSize, this);
}

// ── Poll / data timer ─────────────────────────────────────────────────────────

void SpyWatch::OnDataTimer(wxTimerEvent&)
{
    Poll();
}

void SpyWatch::Poll()
{
    if (!source_ || !source_->IsReady()) return;

    for (const auto& entry : entries_) {
        auto e = source_->Get(entry.key);
        if (!e) continue;

        UpdateEntry(entry.key,
                    type_tag_to_string(e->type_tag),
                    entry_value_to_string(*e));
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void SpyWatch::AddKey(const std::string& key)
{
    for (const auto& e : entries_)
        if (e.key == key) return;

    WatchEntry entry;
    entry.key   = key;
    entry.type  = "—";
    entry.value = "—";
    entries_.push_back(entry);
    RebuildRows();
}

void SpyWatch::RemoveKey(const std::string& key)
{
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&](const WatchEntry& e){ return e.key == key; });
    if (it == entries_.end()) return;
    entries_.erase(it);
    RebuildRows();
}

void SpyWatch::UpdateEntry(const std::string& key,
                           const std::string& type,
                           const std::string& value)
{
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].key != key) continue;

        entries_[i].type  = type;
        entries_[i].value = value;

        if (i < row_widgets_.size()) {
            row_widgets_[i].type_label->SetLabel(wxString::FromUTF8(type));
            row_widgets_[i].value_label->SetLabel(wxString::FromUTF8(value));
            row_widgets_[i].value_label->SetForegroundColour(
                entries_[i].override_active ? WATCH_VALUE_OVR : WATCH_TEXT);
        }
        break;
    }
}

std::vector<WatchEntry> SpyWatch::GetOverrides() const
{
    std::vector<WatchEntry> result;
    for (const auto& e : entries_)
        if (e.override_active) result.push_back(e);
    return result;
}

void SpyWatch::Clear()
{
    entries_.clear();
    RebuildRows();
}

// ── Layout ────────────────────────────────────────────────────────────────────

void SpyWatch::RebuildRows()
{
    inner_->DestroyChildren();
    row_widgets_.clear();

    sizer_ = new wxGridBagSizer(0, 0);
    sizer_->SetEmptyCellSize({ 0, 0 });

    BuildHeaderRow(sizer_);

    for (size_t i = 0; i < entries_.size(); ++i)
        row_widgets_.push_back(
            BuildDataRow(sizer_, static_cast<int>(i) + 1, entries_[i]));

    sizer_->AddGrowableCol(COL_KEY);
    inner_->SetSizerAndFit(sizer_);
    FitInside();
    Refresh();
}

void SpyWatch::BuildHeaderRow(wxGridBagSizer* sizer)
{
    auto make_header = [&](const wxString& text, int col, int width) {
        wxPanel* cell = new wxPanel(inner_, wxID_ANY,
                                    wxDefaultPosition, wxSize(width, HEADER_H));
        cell->SetBackgroundColour(WATCH_HEADER_BG);

        wxStaticText* lbl = new wxStaticText(cell, wxID_ANY, text,
                                              wxDefaultPosition, wxDefaultSize,
                                              wxALIGN_LEFT);
        lbl->SetForegroundColour(WATCH_HEADER_FG);
        lbl->SetFont(font_header_);

        auto* s = new wxBoxSizer(wxHORIZONTAL);
        s->AddSpacer(8);
        s->Add(lbl, 1, wxALIGN_CENTER_VERTICAL);
        cell->SetSizer(s);

        sizer->Add(cell, wxGBPosition(0, col), wxGBSpan(1, 1), wxEXPAND);
    };

    make_header("Key",      COL_KEY,   180);
    make_header("Type",     COL_TYPE,   80);
    make_header("Value",    COL_VALUE, 140);
    make_header("Override", COL_OVR,   200);
}

SpyWatch::RowWidgets SpyWatch::BuildDataRow(wxGridBagSizer* sizer,
                                             int row, WatchEntry& entry)
{
    RowWidgets rw;
    wxColour bg = (row % 2 == 0) ? WATCH_ROW_ALT : WATCH_BG;

    // ── Key ───────────────────────────────────────────────────────────────
    {
        wxPanel* cell = new wxPanel(inner_, wxID_ANY,
                                    wxDefaultPosition, wxSize(-1, ROW_H));
        cell->SetBackgroundColour(bg);

        rw.key_label = new wxStaticText(cell, wxID_ANY,
                                         wxString::FromUTF8(entry.key),
                                         wxDefaultPosition, wxDefaultSize,
                                         wxST_ELLIPSIZE_END);
        rw.key_label->SetFont(font_mono_);
        rw.key_label->SetForegroundColour(WATCH_PRIMARY);

        auto* s = new wxBoxSizer(wxHORIZONTAL);
        s->AddSpacer(8);
        s->Add(rw.key_label, 1, wxALIGN_CENTER_VERTICAL);
        cell->SetSizer(s);
        sizer->Add(cell, wxGBPosition(row, COL_KEY), wxGBSpan(1,1), wxEXPAND);
    }

    // ── Type ──────────────────────────────────────────────────────────────
    {
        wxPanel* cell = new wxPanel(inner_, wxID_ANY,
                                    wxDefaultPosition, wxSize(80, ROW_H));
        cell->SetBackgroundColour(bg);

        rw.type_label = new wxStaticText(cell, wxID_ANY,
                                          wxString::FromUTF8(entry.type));
        rw.type_label->SetFont(font_mono_);
        rw.type_label->SetForegroundColour(WATCH_TEXT);

        auto* s = new wxBoxSizer(wxHORIZONTAL);
        s->AddSpacer(8);
        s->Add(rw.type_label, 1, wxALIGN_CENTER_VERTICAL);
        cell->SetSizer(s);
        sizer->Add(cell, wxGBPosition(row, COL_TYPE), wxGBSpan(1,1), wxEXPAND);
    }

    // ── Value ─────────────────────────────────────────────────────────────
    {
        wxPanel* cell = new wxPanel(inner_, wxID_ANY,
                                    wxDefaultPosition, wxSize(140, ROW_H));
        cell->SetBackgroundColour(bg);

        rw.value_label = new wxStaticText(cell, wxID_ANY,
                                           wxString::FromUTF8(entry.value),
                                           wxDefaultPosition, wxDefaultSize,
                                           wxST_ELLIPSIZE_END);
        rw.value_label->SetFont(font_mono_);
        rw.value_label->SetForegroundColour(
            entry.override_active ? WATCH_VALUE_OVR : WATCH_TEXT);

        auto* s = new wxBoxSizer(wxHORIZONTAL);
        s->AddSpacer(8);
        s->Add(rw.value_label, 1, wxALIGN_CENTER_VERTICAL);
        cell->SetSizer(s);
        sizer->Add(cell, wxGBPosition(row, COL_VALUE), wxGBSpan(1,1), wxEXPAND);
    }

    // ── Override (ToggleBox + text field) ─────────────────────────────────
    {
        wxPanel* cell = new wxPanel(inner_, wxID_ANY,
                                    wxDefaultPosition, wxSize(200, ROW_H));
        cell->SetBackgroundColour(bg);

        size_t idx = static_cast<size_t>(row) - 1;

        rw.ovr_toggle = new ToggleBox(cell, bg, [this, idx](bool checked) {
            OnOverrideToggle(checked, idx);
        });
        rw.ovr_toggle->SetChecked(entry.override_active);

        // Border panel gives the text field a green 1px border
        wxPanel* border = new wxPanel(cell, wxID_ANY,
                                       wxDefaultPosition, wxSize(122, ROW_H - 6));
        border->SetBackgroundColour(WATCH_PRIMARY);

        rw.ovr_field = new wxTextCtrl(border, wxID_ANY,
                                       wxString::FromUTF8(entry.override_value),
                                       wxPoint(1, 1), wxSize(120, ROW_H - 8),
                                       wxTE_PROCESS_ENTER | wxNO_BORDER);
        rw.ovr_field->SetFont(font_mono_);
        rw.ovr_field->SetBackgroundColour(WATCH_FIELD_BG);
        rw.ovr_field->SetForegroundColour(WATCH_TEXT);
        rw.ovr_field->Enable(entry.override_active);

        auto* s = new wxBoxSizer(wxHORIZONTAL);
        s->AddSpacer(8);
        s->Add(rw.ovr_toggle, 0, wxALIGN_CENTER_VERTICAL);
        s->AddSpacer(8);
        s->Add(border, 0, wxALIGN_CENTER_VERTICAL);
        s->AddSpacer(6);
        cell->SetSizer(s);
        sizer->Add(cell, wxGBPosition(row, COL_OVR), wxGBSpan(1,1), wxEXPAND);

        rw.ovr_field->Bind(wxEVT_TEXT_ENTER, [this, idx](wxCommandEvent&) {
            OnOverrideText(idx);
        });
        rw.ovr_field->Bind(wxEVT_KILL_FOCUS, [this, idx](wxFocusEvent& e) {
            OnOverrideText(idx);
            e.Skip();
        });
    }

    return rw;
}

// ── Override callbacks ────────────────────────────────────────────────────────

void SpyWatch::OnOverrideToggle(bool checked, size_t index)
{
    if (index >= entries_.size()) return;

    entries_[index].override_active = checked;
    row_widgets_[index].ovr_field->Enable(checked);
    row_widgets_[index].value_label->SetForegroundColour(
        checked ? WATCH_VALUE_OVR : WATCH_TEXT);

    if (checked) {
        // Seed field with current live value if empty
        if (entries_[index].override_value.empty())
            row_widgets_[index].ovr_field->SetValue(
                wxString::FromUTF8(entries_[index].value));
        row_widgets_[index].ovr_field->SetFocus();
    }
}

void SpyWatch::OnOverrideText(size_t index)
{
    if (index >= entries_.size()) return;
    entries_[index].override_value =
        row_widgets_[index].ovr_field->GetValue().ToStdString();
}

// ── Resize ────────────────────────────────────────────────────────────────────

void SpyWatch::OnSize(wxSizeEvent& e)
{
    if (inner_ && sizer_) {
        inner_->SetSize(GetClientSize().x, inner_->GetSize().y);
        FitInside();
    }
    e.Skip();
}

} // namespace spycat
