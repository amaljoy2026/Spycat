// spysettings.cpp
#include "spysettings.hpp"
#include "../spymap/spymap.hpp"

#include <wx/statline.h>

namespace spycat
{

SpySettings::SpySettings(wxWindow* parent, App& app)
    : wxDialog(parent, wxID_ANY, "Settings",
               wxDefaultPosition, wxSize(360, 260),
               wxDEFAULT_DIALOG_STYLE)
    , app_(app)
{
    SetBackgroundColour(app_.GetTheme().GetPrimaryBackgroundColor());

    // ── App Name ──────────────────────────────────────────────────────────────
    auto* name_label = new wxStaticText(this, wxID_ANY, "App Name");
    name_label->SetFont(app_.GetTheme().GetFont());
    name_label->SetForegroundColour(app_.GetTheme().GetPrimaryTextColor());

    // 1 px HighlightColor border around the text field (same trick as SpyWatch override)
    auto* name_border = new wxPanel(this, wxID_ANY);
    name_border->SetBackgroundColour(app_.GetTheme().GetHighlightColor());

    name_ctrl_ = new wxTextCtrl(name_border, wxID_ANY,
                                wxString::FromUTF8(app_.GetSegmentName()),
                                wxDefaultPosition, wxDefaultSize);
    name_ctrl_->SetFont(app_.GetTheme().GetFont());
    name_ctrl_->SetBackgroundColour(app_.GetTheme().GetSecondaryBackgroundColor());
    name_ctrl_->SetForegroundColour(app_.GetTheme().GetPrimaryTextColor());

    auto* border_sizer = new wxBoxSizer(wxVERTICAL);
    border_sizer->Add(name_ctrl_, 1, wxEXPAND | wxALL, 1);
    name_border->SetSizer(border_sizer);   // SetSizer, not SetSizerAndFit — let it stretch

    auto* name_row = new wxBoxSizer(wxHORIZONTAL);
    name_row->Add(name_label,  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    name_row->Add(name_border, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND);

    // ── Clear App Data ────────────────────────────────────────────────────────
    // 1 px HighlightColor border around a custom panel button.
    // wxButton ignores SetForegroundColour on macOS, so we use wxPanel +
    // wxStaticText for full colour control on hover.
    const wxColour hlCol  = app_.GetTheme().GetHighlightColor();
    const wxColour hlText = app_.GetTheme().GetHighlightTextColor();
    const wxColour bgCol  = app_.GetTheme().GetSecondaryBackgroundColor();
    const wxColour fgCol  = app_.GetTheme().GetPrimaryTextColor();

    auto* clear_border = new wxPanel(this, wxID_ANY);
    clear_border->SetBackgroundColour(hlCol);

    auto* clear_btn = new wxPanel(clear_border, wxID_ANY);
    clear_btn->SetBackgroundColour(bgCol);
    clear_btn->SetCursor(wxCursor(wxCURSOR_HAND));

    auto* clear_lbl = new wxStaticText(clear_btn, wxID_ANY, "Clear App Data",
                                       wxDefaultPosition, wxDefaultSize,
                                       wxALIGN_CENTRE_HORIZONTAL);
    clear_lbl->SetFont(app_.GetTheme().GetFont());
    clear_lbl->SetForegroundColour(fgCol);
    clear_lbl->SetCursor(wxCursor(wxCURSOR_HAND));

    auto* lbl_sizer = new wxBoxSizer(wxHORIZONTAL);
    lbl_sizer->AddStretchSpacer(1);
    lbl_sizer->Add(clear_lbl, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, 4);
    lbl_sizer->AddStretchSpacer(1);
    clear_btn->SetSizer(lbl_sizer);

    auto* clear_border_sizer = new wxBoxSizer(wxVERTICAL);
    clear_border_sizer->Add(clear_btn, 1, wxEXPAND | wxALL, 1);
    clear_border->SetSizerAndFit(clear_border_sizer);

    // Hover: propagate from both the panel and the label inside it
    auto on_enter = [clear_btn, clear_lbl, hlCol, hlText](wxMouseEvent& e) {
        clear_btn->SetBackgroundColour(hlCol);
        clear_lbl->SetForegroundColour(hlText);
        clear_btn->Refresh();
        e.Skip();
    };
    auto on_leave = [clear_btn, clear_lbl, bgCol, fgCol](wxMouseEvent& e) {
        clear_btn->SetBackgroundColour(bgCol);
        clear_lbl->SetForegroundColour(fgCol);
        clear_btn->Refresh();
        e.Skip();
    };
    clear_btn->Bind(wxEVT_ENTER_WINDOW, on_enter);
    clear_btn->Bind(wxEVT_LEAVE_WINDOW, on_leave);
    clear_lbl->Bind(wxEVT_ENTER_WINDOW, on_enter);
    clear_lbl->Bind(wxEVT_LEAVE_WINDOW, on_leave);

    auto on_click = [this](wxMouseEvent&) {
        wxString seg = name_ctrl_->GetValue().Trim();
        if (seg.IsEmpty()) return;

        wxMessageDialog confirm(
            this,
            "Destroy shared memory segment '" + seg + "'?\n\n"
            "All processes attached to this segment will lose their data.",
            "Clear App Data",
            wxYES_NO | wxNO_DEFAULT | wxICON_WARNING);

        if (confirm.ShowModal() == wxID_YES) {
            Spymap::destroy(seg.ToStdString());
            name_ctrl_->SetValue("_test_");
        }
    };
    clear_btn->Bind(wxEVT_LEFT_UP, on_click);
    clear_lbl->Bind(wxEVT_LEFT_UP, on_click);

    // ── Separator ─────────────────────────────────────────────────────────────
    auto* sep = new wxStaticLine(this, wxID_ANY);

    // ── Version Info ──────────────────────────────────────────────────────────
    auto* ver_heading = new wxStaticText(this, wxID_ANY, "Version Info");
    ver_heading->SetFont(app_.GetTheme().GetBoldFont());
    ver_heading->SetForegroundColour(app_.GetTheme().GetHighlightColor());

    auto* ver_text = new wxStaticText(this, wxID_ANY,
        "Spyscope  v1.0.0\n"
        "wxWidgets " wxVERSION_NUM_DOT_STRING "\n"
        "Boost.Interprocess");
    ver_text->SetFont(app_.GetTheme().GetFont());
    ver_text->SetForegroundColour(app_.GetTheme().GetPrimaryTextColor());

    // ── OK / Cancel ───────────────────────────────────────────────────────────
    auto* btn_sizer = CreateButtonSizer(wxOK | wxCANCEL);

    // ── Layout ────────────────────────────────────────────────────────────────
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->AddSpacer(16);
    sizer->Add(name_row,    0, wxEXPAND | wxLEFT | wxRIGHT, 20);
    sizer->AddSpacer(12);
    sizer->Add(clear_border, 0, wxALIGN_RIGHT | wxRIGHT, 20);
    sizer->AddSpacer(16);
    sizer->Add(sep,         0, wxEXPAND | wxLEFT | wxRIGHT, 20);
    sizer->AddSpacer(12);
    sizer->Add(ver_heading, 0, wxLEFT, 20);
    sizer->AddSpacer(4);
    sizer->Add(ver_text,    0, wxLEFT, 20);
    sizer->AddStretchSpacer(1);
    sizer->Add(btn_sizer,   0, wxEXPAND | wxALL, 12);
    SetSizer(sizer);
}

wxString SpySettings::GetSegmentName() const
{
    return name_ctrl_->GetValue().Trim();
}

} // namespace spycat
