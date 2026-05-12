// main.cpp
#include "app.hpp"
#include "datasource.hpp"
#include "spynavigator.hpp"
#include "spyplot.hpp"

using namespace spycat;

bool SpyScope::OnInit()
{
    map_    = new Spymap("_test_");
    source_ = new DataSource(*this);

    wxFrame* frame = new wxFrame(nullptr, wxID_ANY, "SpyPlot Driver",
                                 wxDefaultPosition, wxSize(900, 400));

    nav_ = new SpyNavigator(frame, *this);


    frame->Show(true);
    return true;
}

wxIMPLEMENT_APP(SpyScope);
