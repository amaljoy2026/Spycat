// app.cpp
#include "app.hpp"
#include "mainframe.hpp"
#include "datasource.hpp"
#include "spynav.hpp"
#include "spyplot.hpp"
#include "spywatch.hpp"
#include "spydefault.hpp"

using namespace spycat;

bool App::OnInit()
{
    source_ = new DataSource("_test_");

    frame_ = new MainFrame("Spyscope", *this);
    frame_->SetBackgroundColour(GetTheme().GetSecondaryBackgroundColor());

    wxAuiManager& dock = frame_->GetDock();
    // All content panels are created with SpyDock as parent —
    // SpyDockPane::Reparent fixes up the hierarchy when AddContent is called.
    dock.AddPane(new SpyNav(frame_, *this), wxAuiPaneInfo()
        .Caption("Navigator")
        .CloseButton(true)
        //.MinSize({100, -1}),
        .Left()
    );
    dock.AddPane(new SpyWatch(frame_, *this), wxAuiPaneInfo()
        .Caption("Watch")
        .CloseButton(true)
        //.MinSize({-1, 100}),
        .Bottom()
    );
    dock.AddPane(new SpyDefault(frame_, *this), wxAuiPaneInfo()
        .Caption("")
        .CloseButton(true)
        //.MinSize({-1, 100}),
        .Center()
    );

    dock.Update();
    frame_->Show();
    return true;
}

wxIMPLEMENT_APP(App);
