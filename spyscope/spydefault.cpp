// spydefault.cpp
#include "spydefault.hpp"
#include "spyplot.hpp"

#include <wx/dnd.h>
#include <wx/dcbuffer.h>
#include "dockpanel.hpp"

namespace spycat {

// ── Drop target ───────────────────────────────────────────────────────────────

class DefaultDropTarget : public wxTextDropTarget
{
public:
    DefaultDropTarget(SpyDefault* panel) : panel_(panel) {}

    bool OnDropText(wxCoord, wxCoord, const wxString& text) override
    {
        panel_->Promote(text.ToStdString());
        return true;
    }

private:
    SpyDefault* panel_;
};

// ── Construction ──────────────────────────────────────────────────────────────

SpyDefault::SpyDefault(wxWindow* parent, App& app, wxWindowID id)
    : wxPanel(parent, id)
    , app_(app)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(app_.GetTheme().GetPrimaryBackgroundColor());
    SetMinSize({ 200, 100 });

    Bind(wxEVT_PAINT, &SpyDefault::OnPaint, this);

    SetDropTarget(new DefaultDropTarget(this));

    // Intercept AUI pane-close events so SpyDefault can never be closed
    app_.GetDockPanel()->Bind(wxEVT_AUI_PANE_CLOSE, &SpyDefault::OnPaneClose, this);
}

// ── Promotion ─────────────────────────────────────────────────────────────────

void SpyDefault::Promote(const std::string& text)
{
    if (promoted_) return;
    promoted_ = true;

    // Detach our drop target — SpyPlot will install its own
    SetDropTarget(nullptr);

    // Split \n-joined multi-key payload; first key seeds the plot
    wxArrayString keys = wxSplit(wxString::FromUTF8(text), '\n');

    std::string first_key;
    for (const auto& k : keys)
        if (!k.IsEmpty()) { first_key = k.ToStdString(); break; }

    if (first_key.empty()) { promoted_ = false; SetDropTarget(new DefaultDropTarget(this)); return; }

    auto* plot = new SpyPlot(this, app_, first_key);
    for (const auto& k : keys)
        if (!k.IsEmpty() && k.ToStdString() != first_key)
            plot->AddTrace(k.ToStdString());

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(plot, 1, wxEXPAND);
    SetSizerAndFit(sizer);
    app_.GetDockPanel()->GetDock().Update();
    Layout();
}

// ── Reset ─────────────────────────────────────────────────────────────────────

void SpyDefault::Reset()
{
    promoted_ = false;

    // Destroy the SpyPlot child and remove the sizer
    DestroyChildren();
    SetSizer(nullptr);

    // Re-register drop target (was cleared on promotion)
    SetDropTarget(new DefaultDropTarget(this));

    Refresh();
    app_.GetDockPanel()->GetDock().Update();
}

// ── Pane close interception ───────────────────────────────────────────────────

void SpyDefault::OnPaneClose(wxAuiManagerEvent& e)
{
    // Only handle our own pane; let all others proceed normally
    if (e.GetPane()->window != this) {
        e.Skip();
        return;
    }

    // SpyDefault never closes — veto unconditionally
    e.Veto();

    // If a plot is loaded, closing means "clear it"
    if (promoted_)
        Reset();
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void SpyDefault::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    wxSize sz = GetSize();

    // Background
    dc.SetBackground(wxBrush(app_.GetTheme().GetPrimaryBackgroundColor()));
    dc.Clear();

    if (promoted_) return;   // SpyPlot child covers us

    // Dashed drop-zone border, inset 12 px
    dc.SetPen(wxPen(app_.GetTheme().GetHighlightColor(), 2, wxPENSTYLE_SHORT_DASH));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(12, 12, sz.x - 24, sz.y - 24, 6);

    // Centered label
    dc.SetFont(app_.GetTheme().GetBoldFont());
    dc.SetTextForeground(app_.GetTheme().GetHighlightColor());
    wxString msg = "Drop a key here";
    wxSize   ts  = dc.GetTextExtent(msg);
    dc.DrawText(msg, (sz.x - ts.x) / 2, (sz.y - ts.y) / 2);
}

} // namespace spycat
