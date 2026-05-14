// app.cpp
#include "app.hpp"
#include "dockpanel.hpp"
#include "datasource.hpp"
#include "spynav.hpp"
#include "spyplot.hpp"
#include "spywatch.hpp"
#include "spydefault.hpp"
#include "logo.h"

using namespace spycat;

wxPanel *App::CreateTopbar()
{
    wxPanel *topbar = new wxPanel(frame_, wxID_ANY, {0, 0}, {-1, 20}, wxBORDER_NONE, wxEmptyString);
    topbar->SetBackgroundColour(GetTheme().GetSecondaryBackgroundColor());
    wxStaticBitmap *logo = new wxStaticBitmap(topbar, wxID_ANY, GetSpyCatLogoBitmap(), {0, 0}, {32, 32});
    wxSizer *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(logo, 0, wxALIGN_CENTER_VERTICAL);
    topbar->SetSizer(sizer);
    sizer->Fit(topbar);
    sizer->SetSizeHints(topbar);
    return topbar;
}

bool App::OnInit()
{
    wxInitAllImageHandlers(); // <-- add this line

    source_ = new DataSource("_test_");

    frame_ = new wxFrame(nullptr, wxID_ANY, "Spyscope", wxDefaultPosition, wxSize(1024, 768));
    frame_->SetBackgroundColour(GetTheme().GetSecondaryBackgroundColor());
    
    topbar_ = CreateTopbar();

    dockpanel_ = new DockPanel(frame_, *this);
    dockpanel_->SetBackgroundColour(GetTheme().GetSecondaryBackgroundColor());

    sizer_ = new wxBoxSizer(wxVERTICAL);
    sizer_->Add(topbar_, 0, wxEXPAND);
    sizer_->Add(dockpanel_, 1, wxEXPAND);
    frame_->SetSizer(sizer_);
    frame_->SetAutoLayout(true);


    wxAuiManager& dock = dockpanel_->GetDock();
    // All content panels are created with SpyDock as parent —
    // SpyDockPane::Reparent fixes up the hierarchy when AddContent is called.
    dock.AddPane(new SpyNav(dockpanel_, *this), wxAuiPaneInfo()
        .Caption("Navigator")
        .CloseButton(false)
        .MinSize({100, -1})
        .Left()
    );
    watch_ = new SpyWatch(dockpanel_, *this);
    dock.AddPane(watch_, wxAuiPaneInfo()
        .Caption("Watch")
        .CloseButton(true)
        .MinSize({-1, 100})
        .Bottom()
    );
    dock.AddPane(new SpyDefault(dockpanel_, *this), wxAuiPaneInfo()
        .Caption("")
        .CloseButton(true)
        //.MinSize({-1, 100}),
        .Center()
    );

    dock.Update();
    frame_->Show();
    return true;
}

void App::AddPlotPane(const std::string& key)
{
    wxAuiManager& dock = dockpanel_->GetDock();

    // Use only the leaf name (after the last '.') as the caption
    wxString caption = wxString::FromUTF8(key);
    int dot = caption.Find('.', /*fromEnd=*/true);
    if (dot != wxNOT_FOUND)
        caption = caption.Mid(dot + 1);

    SpyPlot* plot = new SpyPlot(dockpanel_, *this, key);
    dock.AddPane(plot, wxAuiPaneInfo()
        .Caption(caption)
        .CloseButton(true)
        .Right()
        .MinSize({200, 150})
        .BestSize({300, 200})
        .Layer(1)
    );
    dock.Update();
}

void App::AddWatchPane(const std::string& key)
{
    wxAuiManager& dock = dockpanel_->GetDock();

    wxString caption = wxString::FromUTF8(key);
    int dot = caption.Find('.', /*fromEnd=*/true);
    if (dot != wxNOT_FOUND)
        caption = caption.Mid(dot + 1);

    SpyWatch* watch = new SpyWatch(dockpanel_, *this);
    watch->AddKey(key);
    dock.AddPane(watch, wxAuiPaneInfo()
        .Caption(caption)
        .CloseButton(true)
        .Right()
        .MinSize({250, 80})
        .BestSize({350, 120})
        .Layer(1)
    );
    dock.Update();
}

wxIMPLEMENT_APP(App);
