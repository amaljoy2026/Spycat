// spydefault.cpp
#include "spydefault.hpp"
#include "spyplot.hpp"

#include <wx/dnd.h>
#include <wx/dcbuffer.h>
#include "mainframe.hpp"

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
}

// ── Promotion ─────────────────────────────────────────────────────────────────

void SpyDefault::Promote(const std::string& key)
{
    if (promoted_) return;
    promoted_ = true;

    // Detach our drop target — SpyPlot will install its own
    SetDropTarget(nullptr);

    // Create the plot as a child that fills this panel
    auto* plot = new SpyPlot(this, app_, key);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(plot, 1, wxEXPAND);
    SetSizerAndFit(sizer);
    app_.GetMainFrame()->GetDock().Update();
    Layout();
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
