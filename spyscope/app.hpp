// app.hpp
#ifndef __SPYSCOPE_APP_HPP__
#define __SPYSCOPE_APP_HPP__

#include <wx/wx.h>
#include "../spymap/spymap.hpp"
#include "theme.hpp"

// Forward-declare spycat types — full definitions included in main.cpp
namespace spycat
{
    class DataSource;
    class SpyNav;
    class SpyPlot;
    class SpyWatch;
    class DockPanel;
}


namespace spycat
{

class App : public wxApp
{
public:
    bool OnInit() override;
    DataSource* GetDataSource() { return source_; }
    Theme& GetTheme() { return theme_; }
    DockPanel *GetDockPanel() { return dockpanel_; }
    SpyWatch*  GetSpyWatch()  { return watch_; }

    // Dynamically add a new plot or watch pane to the right dock
    void AddPlotPane(const std::string& key);
    void AddWatchPane(const std::string& key);

private:
    wxPanel *CreateTopbar();

    DataSource*   source_    = nullptr;
    Theme         theme_;
    DockPanel*    dockpanel_ = nullptr;
    SpyWatch*     watch_     = nullptr;
    wxPanel*      topbar_    = nullptr;
    wxFrame*      frame_     = nullptr;
    wxSizer*      sizer_     = nullptr;
};

}

#endif // __SPYSCOPE_APP_HPP__
