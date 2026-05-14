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
    wxPanel *topbar = new wxPanel(frame_, wxID_ANY, {0, 0}, {-1, 42}, wxBORDER_NONE, wxEmptyString);
    topbar->SetBackgroundColour(GetTheme().GetSecondaryBackgroundColor());
    wxStaticBitmap *logo = new wxStaticBitmap(topbar, wxID_ANY, GetSpyCatLogoBitmap(), {0, 0}, {32, 32});
    
    wxStaticText *title = new wxStaticText(topbar, wxID_ANY, "Spyscope");
    title->SetFont(GetTheme().GetFont());
    title->SetForegroundColour(GetTheme().GetPrimaryTextColor());
    
    wxSizer *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(logo, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(title, 0, wxALIGN_CENTER_VERTICAL);
    topbar->SetSizer(sizer);
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
    watch_ = new SpyWatch(dockpanel_, *this);
    dock.AddPane(watch_, wxAuiPaneInfo()
        .Caption("Watch")
        .CloseButton(true)
        .MinSize({-1, 220})
        .Bottom()
    );
    dock.AddPane(new SpyNav(dockpanel_, *this), wxAuiPaneInfo()
        .Caption("Navigator")
        .CloseButton(false)
        .MinSize({180, -1})
        .Left()
        .Layer(1)   // outer layer — spans full height beside the bottom dock
    );
    dock.AddPane(new SpyDefault(dockpanel_, *this), wxAuiPaneInfo()
        .Caption("")
        .CloseButton(true)
        //.MinSize({-1, 100}),
        .Center()
    );

    dock.Update();
    frame_->Show();

    // Master data poll timer — all registered DataObservers are notified each tick
    data_timer_.Bind(wxEVT_TIMER, &App::OnDataTimer, this);
    data_timer_.Start(17);

    return true;
}

void App::OnDataTimer(wxTimerEvent&)
{
    for (auto* obs : observers_)
        obs->OnDataPoll();
}

void App::AddPlotPane(const std::vector<std::string>& keys)
{
    if (keys.empty()) return;
    wxAuiManager& dock = dockpanel_->GetDock();

    // Single key → leaf name caption; multiple → generic "Plot"
    wxString caption;
    if (keys.size() == 1) {
        caption = wxString::FromUTF8(keys[0]);
        int dot = caption.Find('.', /*fromEnd=*/true);
        if (dot != wxNOT_FOUND) caption = caption.Mid(dot + 1);
    } else {
        caption = "Plot";
    }

    SpyPlot* plot = new SpyPlot(dockpanel_, *this, keys[0]);
    for (size_t i = 1; i < keys.size(); ++i)
        plot->AddTrace(keys[i]);

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

void App::AddWatchPane(const std::vector<std::string>& keys)
{
    if (keys.empty()) return;
    wxAuiManager& dock = dockpanel_->GetDock();

    wxString caption;
    if (keys.size() == 1) {
        caption = wxString::FromUTF8(keys[0]);
        int dot = caption.Find('.', /*fromEnd=*/true);
        if (dot != wxNOT_FOUND) caption = caption.Mid(dot + 1);
    } else {
        caption = "Watch";
    }

    SpyWatch* watch = new SpyWatch(dockpanel_, *this);
    for (const auto& key : keys)
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
