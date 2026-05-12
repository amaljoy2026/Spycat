// spyplot_main.cpp
// Minimal wxWidgets driver for SpyPlot development.

#include <wx/wx.h>
#include <cmath>

#include "../spymap/spymap.hpp"
#include "spyplot.hpp"
#include "spywatch.hpp"
#include "spynavigator.hpp"

using namespace spycat;

class SpyPlotApp : public wxApp
{
public:
    bool OnInit() override
    {
        map_ = new Spymap("_test_");
        source_ = new DataSource(map_);
        
        wxFrame* frame = new wxFrame(nullptr, wxID_ANY, "SpyPlot Driver",
                                     wxDefaultPosition, wxSize(900, 400));
        
        nav_ = new SpyNavigator(frame, source_);
        
        timer = new wxTimer();
        timer->Bind(wxEVT_TIMER, &SpyPlotApp::OnTimer, this);
        timer->Start(17);
        frame->Show(true);
        return true;
    }

    void OnTimer(wxEvent& event)
    {
        source_->Poll();
        nav_->Poll();
    }

    wxTimer *timer;
    Spymap *map_; 
    DataSource *source_;
    SpyNavigator *nav_;
    
};

wxIMPLEMENT_APP(SpyPlotApp);