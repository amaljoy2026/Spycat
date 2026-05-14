// spywatch.cpp
#include "spywatch.hpp"

#include <wx/dnd.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <variant>

#include "dockpanel.hpp"

namespace spycat
{

// ── Drop target ───────────────────────────────────────────────────────────────

class WatchDropTarget : public wxTextDropTarget
{
public:
    WatchDropTarget(SpyWatch* watch) : watch_(watch) {}

    bool OnDropText(wxCoord /*x*/, wxCoord y, const wxString& text) override
    {
        int target_row = watch_->RowFromClientY(y);
        for (const auto& key : wxSplit(text, '\n')) {
            if (key.IsEmpty()) continue;
            std::string k = key.ToStdString();
            // Key already in this watch → reorder; otherwise add at end
            watch_->MoveKeyToRow(k, target_row);   // no-op if key not present; falls through to Add
            watch_->AddKey(k);                      // no-op if key already present
        }
        return true;
    }

private:
    SpyWatch* watch_;
};

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

SpyWatch::SpyWatch(wxWindow* parent, App& app, wxWindowID id)
    : wxScrolledWindow(parent, id)
    , app_(app)
{
    app_.RegisterObserver(this);

    SetBackgroundColour(app_.GetTheme().GetPrimaryBackgroundColor());
    SetScrollRate(0, ROW_H);

    inner_ = new wxPanel(this, wxID_ANY);
    inner_->SetBackgroundColour(app_.GetTheme().GetPrimaryBackgroundColor());

    RebuildRows();

    auto* outer = new wxBoxSizer(wxVERTICAL);
    outer->Add(inner_, 0, wxEXPAND);
    SetSizer(outer);

    Bind(wxEVT_SIZE, &SpyWatch::OnSize, this);

    SetDropTarget(new WatchDropTarget(this));
}

// ── Destructor ────────────────────────────────────────────────────────────────

SpyWatch::~SpyWatch()
{
    app_.UnregisterObserver(this);
}

// ── Poll / data timer ─────────────────────────────────────────────────────────

void SpyWatch::OnDataPoll()
{
    Poll();
}

void SpyWatch::Poll()
{
    DataSource *source = app_.GetDataSource();

    if (!source || !source->IsReady()) return;

    for (const auto& entry : entries_) {
        auto e = source->Get(entry.key);
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
    app_.GetDockPanel()->GetDock().Update();
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
                entries_[i].override_active ? app_.GetTheme().GetHighlightColor() : app_.GetTheme().GetPrimaryTextColor());
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

int SpyWatch::RowFromClientY(int y) const
{
    // y is in SpyWatch (scrolled window) client coords.
    // Convert to unscrolled inner_ coords: scroll rate is ROW_H px per unit.
    int scroll_units = 0;
    GetViewStart(nullptr, &scroll_units);
    int y_inner = y + scroll_units * ROW_H - HEADER_H;
    if (y_inner < 0) return 0;
    int row = y_inner / ROW_H;
    return std::min(row, static_cast<int>(entries_.size()));
}

void SpyWatch::MoveKeyToRow(const std::string& key, int target_row)
{
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&](const WatchEntry& e){ return e.key == key; });
    if (it == entries_.end()) return;   // key not in this watch — caller handles add
    int src_row = static_cast<int>(std::distance(entries_.begin(), it));
    target_row  = std::max(0, std::min(target_row, static_cast<int>(entries_.size()) - 1));

    if (src_row == target_row) return;  // already in place

    WatchEntry moved = *it;
    entries_.erase(it);
    // After removal, indices above src_row shift down by one
    if (target_row > src_row) --target_row;
    entries_.insert(entries_.begin() + target_row, std::move(moved));
    RebuildRows();
}


// ── Layout ────────────────────────────────────────────────────────────────────

void SpyWatch::RebuildRows()
{
    inner_->DestroyChildren();
    row_widgets_.clear();
    sizer_ = nullptr;

    if (portrait_) {
        auto* flex = new wxFlexGridSizer(0, 2, 0, 0);
        flex->AddGrowableCol(1);
        BuildPortraitRows(flex);
        inner_->SetSizerAndFit(flex);
    } else {
        sizer_ = new wxGridBagSizer(0, 0);
        sizer_->SetEmptyCellSize({ 0, 0 });
        BuildHeaderRow(sizer_);
        for (size_t i = 0; i < entries_.size(); ++i)
            row_widgets_.push_back(
                BuildDataRow(sizer_, static_cast<int>(i) + 1, entries_[i]));
        sizer_->AddGrowableCol(COL_KEY);
        inner_->SetSizerAndFit(sizer_);
    }

    FitInside();
    Refresh();
}

void SpyWatch::BuildHeaderRow(wxGridBagSizer* sizer)
{
    auto make_header = [&](const wxString& text, int col, int width) {
        wxPanel* cell = new wxPanel(inner_, wxID_ANY,
                                    wxDefaultPosition, wxSize(width, HEADER_H));
        cell->SetBackgroundColour(app_.GetTheme().GetHighlightColor());

        wxStaticText* lbl = new wxStaticText(cell, wxID_ANY, text,
                                              wxDefaultPosition, wxDefaultSize,
                                              wxALIGN_LEFT);
        lbl->SetForegroundColour(app_.GetTheme().GetHighlightTextColor());
        lbl->SetFont(app_.GetTheme().GetBoldFont());

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
    wxColour bg = (row % 2 == 0) ? app_.GetTheme().GetAltBackgroundColor() : app_.GetTheme().GetPrimaryBackgroundColor();

    // Right-click any cell → Delete menu; left-down on key cell → drag to plot/watch
    std::string row_key = entry.key;

    auto bind_drag = [this, row_key](wxWindow* w) {
        // NOTE: do NOT capture w in the inner lambda — DoDragDrop can trigger
        // RebuildRows (via MoveKeyToRow) which calls DestroyChildren, making w
        // a dangling pointer before the lambda returns. Use 'this' (SpyWatch)
        // as the drop source window and release capture only on still-live objects.
        w->Bind(wxEVT_LEFT_DOWN, [this, row_key](wxMouseEvent&) {
            wxTextDataObject data(wxString::FromUTF8(row_key));
            wxDropSource source(data, this);
            source.DoDragDrop(wxDrag_CopyOnly);
            if (inner_ && inner_->HasCapture()) inner_->ReleaseMouse();
            if (HasCapture())                   ReleaseMouse();
        });
    };

    auto bind_ctx = [this, row_key](wxWindow* w) {
        w->Bind(wxEVT_CONTEXT_MENU, [this, row_key](wxContextMenuEvent&) {
            const int ID_DELETE = wxID_HIGHEST + 401;
            wxMenu menu;
            menu.Append(ID_DELETE, "Delete");
            menu.Bind(wxEVT_MENU, [this, row_key](wxCommandEvent&) {
                RemoveKey(row_key);
            }, ID_DELETE);
            PopupMenu(&menu);
        });
    };

    // ── Key ───────────────────────────────────────────────────────────────
    {
        wxPanel* cell = new wxPanel(inner_, wxID_ANY,
                                    wxDefaultPosition, wxSize(-1, ROW_H));
        cell->SetBackgroundColour(bg);

        rw.key_label = new wxStaticText(cell, wxID_ANY,
                                         wxString::FromUTF8(entry.key),
                                         wxDefaultPosition, wxDefaultSize,
                                         wxST_ELLIPSIZE_END);
        rw.key_label->SetFont(app_.GetTheme().GetFont());
        rw.key_label->SetForegroundColour(app_.GetTheme().GetPrimaryTextColor());

        auto* s = new wxBoxSizer(wxHORIZONTAL);
        s->AddSpacer(8);
        s->Add(rw.key_label, 1, wxALIGN_CENTER_VERTICAL);
        cell->SetSizer(s);
        sizer->Add(cell, wxGBPosition(row, COL_KEY), wxGBSpan(1,1), wxEXPAND);
        bind_ctx(cell);  bind_ctx(rw.key_label);
        bind_drag(cell); bind_drag(rw.key_label);
    }

    // ── Type ──────────────────────────────────────────────────────────────
    {
        wxPanel* cell = new wxPanel(inner_, wxID_ANY,
                                    wxDefaultPosition, wxSize(80, ROW_H));
        cell->SetBackgroundColour(bg);

        rw.type_label = new wxStaticText(cell, wxID_ANY,
                                          wxString::FromUTF8(entry.type));
        rw.type_label->SetFont(app_.GetTheme().GetFont());
        rw.type_label->SetForegroundColour(app_.GetTheme().GetPrimaryTextColor());

        auto* s = new wxBoxSizer(wxHORIZONTAL);
        s->AddSpacer(8);
        s->Add(rw.type_label, 1, wxALIGN_CENTER_VERTICAL);
        cell->SetSizer(s);
        sizer->Add(cell, wxGBPosition(row, COL_TYPE), wxGBSpan(1,1), wxEXPAND);
        bind_ctx(cell); bind_ctx(rw.type_label);
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
        rw.value_label->SetFont(app_.GetTheme().GetFont());
        rw.value_label->SetForegroundColour(
            entry.override_active ? app_.GetTheme().GetHighlightColor() : app_.GetTheme().GetPrimaryTextColor());

        auto* s = new wxBoxSizer(wxHORIZONTAL);
        s->AddSpacer(8);
        s->Add(rw.value_label, 1, wxALIGN_CENTER_VERTICAL);
        cell->SetSizer(s);
        sizer->Add(cell, wxGBPosition(row, COL_VALUE), wxGBSpan(1,1), wxEXPAND);
        bind_ctx(cell); bind_ctx(rw.value_label);
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
        rw.ovr_field->SetFont(app_.GetTheme().GetFont());
        rw.ovr_field->SetBackgroundColour(app_.GetTheme().GetPrimaryBackgroundColor());
        rw.ovr_field->SetForegroundColour(app_.GetTheme().GetPrimaryTextColor());
        rw.ovr_field->Enable(entry.override_active);

        auto* s = new wxBoxSizer(wxHORIZONTAL);
        s->AddSpacer(8);
        s->Add(rw.ovr_toggle, 0, wxALIGN_CENTER_VERTICAL);
        s->AddSpacer(8);
        s->Add(border, 0, wxALIGN_CENTER_VERTICAL);
        s->AddSpacer(6);
        cell->SetSizer(s);
        sizer->Add(cell, wxGBPosition(row, COL_OVR), wxGBSpan(1,1), wxEXPAND);
        bind_ctx(cell);

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

// ── Portrait layout ───────────────────────────────────────────────────────────

void SpyWatch::BuildPortraitRows(wxSizer* flex)
{
    // Helper: make a left-column label cell
    auto make_label = [&](const wxString& text, const wxColour& bg,
                          bool header, int height) -> wxPanel*
    {
        wxPanel* cell = new wxPanel(inner_, wxID_ANY,
                                    wxDefaultPosition, wxSize(80, height));
        cell->SetBackgroundColour(bg);

        wxStaticText* lbl = new wxStaticText(cell, wxID_ANY, text);
        lbl->SetFont(header ? app_.GetTheme().GetBoldFont()
                             : app_.GetTheme().GetFont());
        lbl->SetForegroundColour(header ? app_.GetTheme().GetHighlightTextColor()
                                        : app_.GetTheme().GetPrimaryTextColor());

        auto* s = new wxBoxSizer(wxHORIZONTAL);
        s->AddSpacer(8);
        s->Add(lbl, 1, wxALIGN_CENTER_VERTICAL);
        cell->SetSizer(s);
        return cell;
    };

    for (size_t i = 0; i < entries_.size(); ++i) {
        WatchEntry& entry = entries_[i];
        RowWidgets   rw;

        const wxColour bg     = (i % 2 == 0)
                                ? app_.GetTheme().GetPrimaryBackgroundColor()
                                : app_.GetTheme().GetAltBackgroundColor();
        const wxColour hdr_bg = app_.GetTheme().GetHighlightColor();
        const wxColour hdr_fg = app_.GetTheme().GetHighlightTextColor();

        // Right-click → Delete menu; left-down on key cell → drag to plot/watch
        std::string row_key = entry.key;

        auto bind_drag = [this, row_key](wxWindow* w) {
            w->Bind(wxEVT_LEFT_DOWN, [this, row_key](wxMouseEvent&) {
                wxTextDataObject data(wxString::FromUTF8(row_key));
                wxDropSource source(data, this);
                source.DoDragDrop(wxDrag_CopyOnly);
                if (inner_ && inner_->HasCapture()) inner_->ReleaseMouse();
                if (HasCapture())                   ReleaseMouse();
            });
        };

        auto bind_ctx = [this, row_key](wxWindow* w) {
            w->Bind(wxEVT_CONTEXT_MENU, [this, row_key](wxContextMenuEvent&) {
                const int ID_DELETE = wxID_HIGHEST + 401;
                wxMenu menu;
                menu.Append(ID_DELETE, "Delete");
                menu.Bind(wxEVT_MENU, [this, row_key](wxCommandEvent&) {
                    RemoveKey(row_key);
                }, ID_DELETE);
                PopupMenu(&menu);
            });
        };

        // ── Key row — header style ────────────────────────────────────────
        flex->Add(make_label("Key", hdr_bg, true, HEADER_H), 0, wxEXPAND);
        {
            wxPanel* cell = new wxPanel(inner_, wxID_ANY,
                                         wxDefaultPosition, wxSize(-1, HEADER_H));
            cell->SetBackgroundColour(hdr_bg);
            rw.key_label = new wxStaticText(cell, wxID_ANY,
                                             wxString::FromUTF8(entry.key),
                                             wxDefaultPosition, wxDefaultSize,
                                             wxST_ELLIPSIZE_END);
            rw.key_label->SetFont(app_.GetTheme().GetBoldFont());
            rw.key_label->SetForegroundColour(hdr_fg);
            auto* s = new wxBoxSizer(wxHORIZONTAL);
            s->AddSpacer(8);
            s->Add(rw.key_label, 1, wxALIGN_CENTER_VERTICAL);
            cell->SetSizer(s);
            flex->Add(cell, 0, wxEXPAND);
            bind_ctx(cell);  bind_ctx(rw.key_label);
            bind_drag(cell); bind_drag(rw.key_label);
        }

        // ── Type row ─────────────────────────────────────────────────────
        flex->Add(make_label("Type", bg, false, ROW_H), 0, wxEXPAND);
        {
            wxPanel* cell = new wxPanel(inner_, wxID_ANY,
                                         wxDefaultPosition, wxSize(-1, ROW_H));
            cell->SetBackgroundColour(bg);
            rw.type_label = new wxStaticText(cell, wxID_ANY,
                                              wxString::FromUTF8(entry.type));
            rw.type_label->SetFont(app_.GetTheme().GetFont());
            rw.type_label->SetForegroundColour(app_.GetTheme().GetPrimaryTextColor());
            auto* s = new wxBoxSizer(wxHORIZONTAL);
            s->AddSpacer(8);
            s->Add(rw.type_label, 1, wxALIGN_CENTER_VERTICAL);
            cell->SetSizer(s);
            flex->Add(cell, 0, wxEXPAND);
        }

        // ── Value row ─────────────────────────────────────────────────────
        flex->Add(make_label("Value", bg, false, ROW_H), 0, wxEXPAND);
        {
            wxPanel* cell = new wxPanel(inner_, wxID_ANY,
                                         wxDefaultPosition, wxSize(-1, ROW_H));
            cell->SetBackgroundColour(bg);
            rw.value_label = new wxStaticText(cell, wxID_ANY,
                                               wxString::FromUTF8(entry.value),
                                               wxDefaultPosition, wxDefaultSize,
                                               wxST_ELLIPSIZE_END);
            rw.value_label->SetFont(app_.GetTheme().GetFont());
            rw.value_label->SetForegroundColour(
                entry.override_active ? app_.GetTheme().GetHighlightColor()
                                      : app_.GetTheme().GetPrimaryTextColor());
            auto* s = new wxBoxSizer(wxHORIZONTAL);
            s->AddSpacer(8);
            s->Add(rw.value_label, 1, wxALIGN_CENTER_VERTICAL);
            cell->SetSizer(s);
            flex->Add(cell, 0, wxEXPAND);
        }

        // ── Override row ──────────────────────────────────────────────────
        flex->Add(make_label("Override", bg, false, ROW_H), 0, wxEXPAND);
        {
            wxPanel* cell = new wxPanel(inner_, wxID_ANY,
                                         wxDefaultPosition, wxSize(-1, ROW_H));
            cell->SetBackgroundColour(bg);

            size_t idx = i;
            rw.ovr_toggle = new ToggleBox(cell, bg, [this, idx](bool checked) {
                OnOverrideToggle(checked, idx);
            });
            rw.ovr_toggle->SetChecked(entry.override_active);

            wxPanel* border = new wxPanel(cell, wxID_ANY,
                                           wxDefaultPosition, wxSize(122, ROW_H - 6));
            border->SetBackgroundColour(WATCH_PRIMARY);
            rw.ovr_field = new wxTextCtrl(border, wxID_ANY,
                                           wxString::FromUTF8(entry.override_value),
                                           wxPoint(1, 1), wxSize(120, ROW_H - 8),
                                           wxTE_PROCESS_ENTER | wxNO_BORDER);
            rw.ovr_field->SetFont(app_.GetTheme().GetFont());
            rw.ovr_field->SetBackgroundColour(app_.GetTheme().GetPrimaryBackgroundColor());
            rw.ovr_field->SetForegroundColour(app_.GetTheme().GetPrimaryTextColor());
            rw.ovr_field->Enable(entry.override_active);

            rw.ovr_field->Bind(wxEVT_TEXT_ENTER, [this, idx](wxCommandEvent&) {
                OnOverrideText(idx);
            });
            rw.ovr_field->Bind(wxEVT_KILL_FOCUS, [this, idx](wxFocusEvent& fe) {
                OnOverrideText(idx); fe.Skip();
            });

            auto* s = new wxBoxSizer(wxHORIZONTAL);
            s->AddSpacer(8);
            s->Add(rw.ovr_toggle, 0, wxALIGN_CENTER_VERTICAL);
            s->AddSpacer(8);
            s->Add(border, 0, wxALIGN_CENTER_VERTICAL);
            s->AddSpacer(6);
            cell->SetSizer(s);
            flex->Add(cell, 0, wxEXPAND);
        }

        row_widgets_.push_back(rw);
    }
}

// ── Override callbacks ────────────────────────────────────────────────────────

void SpyWatch::OnOverrideToggle(bool checked, size_t index)
{
    if (index >= entries_.size()) return;

    entries_[index].override_active = checked;
    row_widgets_[index].ovr_field->Enable(checked);
    row_widgets_[index].value_label->SetForegroundColour(
        checked ? app_.GetTheme().GetHighlightColor() : app_.GetTheme().GetPrimaryTextColor());

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
    wxSize sz = GetClientSize();
    bool is_portrait = sz.y > sz.x;

    if (is_portrait != portrait_) {
        portrait_ = is_portrait;
        RebuildRows();
    }

    if (inner_) {
        inner_->SetSize(sz.x, inner_->GetSize().y);
        FitInside();
    }
    e.Skip();
}

} // namespace spycat
