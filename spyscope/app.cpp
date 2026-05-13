// main.cpp
#include "app.hpp"
#include "datasource.hpp"
#include "spynav.hpp"
#include "spyplot.hpp"
#include "spywatch.hpp"

#include <wx/splitter.h>

using namespace spycat;

#include <wx/wx.h>
#include <wx/aui/aui.h>
#include "mainframe.hpp"
#include <wx/textctrl.h>

#include "spynav.hpp"
#include "spyplot.hpp"
#include "spywatch.hpp"

bool App::OnInit() {
    source_ = new DataSource("_test_");
    frame_ = new MainFrame("Docking Test");
    frame_->SetBackgroundColour(GetTheme().GetSecondaryBackgroundColor());

    SpyNav* nav = new SpyNav(frame_, *this);
    SpyPlot *plot = new SpyPlot(frame_, *this);
    SpyWatch *watch = new SpyWatch(frame_, *this);    

    // frame_->GetDock().AddPane(new SpyNav(frame_, *this), wxAuiPaneInfo()
    //     .Caption("Navigator").CloseButton().Left().MinSize({1000, 100}));
    frame_->GetDock().AddPane(nav, wxAuiPaneInfo().Name("nav").Caption("Navigator").Left());
    frame_->GetDock().AddPane(watch, wxAuiPaneInfo().Name("watch").Caption("Watch").Bottom());
    frame_->GetDock().AddPane(plot, wxAuiPaneInfo().Name("plot").CenterPane());
    frame_->GetDock().Update();

    frame_->Show();
    return true;
}

wxIMPLEMENT_APP(App);


