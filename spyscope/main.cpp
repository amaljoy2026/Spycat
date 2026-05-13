// main.cpp
#include "app.hpp"
#include "datasource.hpp"
#include "spynavigator.hpp"
#include "spyplot.hpp"
#include "spywatch.hpp"

#include <wx/splitter.h>

using namespace spycat;

bool SpyScope::OnInit()
{
    map_    = new Spymap("_test_");
    source_ = new DataSource(*this);

    wxFrame* frame = new wxFrame(nullptr, wxID_ANY, "SpyScope",
                                 wxDefaultPosition, wxSize(1200, 700));

    // ── Outer splitter — left: Navigator | right: plot+watch ─────────────────
    auto* outer = new wxSplitterWindow(frame, wxID_ANY,
                                       wxDefaultPosition, wxDefaultSize,
                                       wxSP_LIVE_UPDATE | wxSP_3DSASH);

    nav_ = new SpyNavigator(outer, *this);

    // ── Inner splitter — top: SpyPlot | bottom: SpyWatch ─────────────────────
    auto* inner = new wxSplitterWindow(outer, wxID_ANY,
                                       wxDefaultPosition, wxDefaultSize,
                                       wxSP_LIVE_UPDATE | wxSP_3DSASH);

    plot_  = new SpyPlot (inner, *this);
    watch_ = new SpyWatch(inner, *this);

    plot_->SetKey("data");
    watch_->AddKey("data");

    inner->SplitHorizontally(plot_, watch_, 400);
    inner->SetMinimumPaneSize(80);

    outer->SplitVertically(nav_, inner, 250);
    outer->SetMinimumPaneSize(150);

    frame->Show(true);
    return true;
}

wxIMPLEMENT_APP(SpyScope);
