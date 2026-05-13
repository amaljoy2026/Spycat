// app.hpp
#ifndef __SPYSCOPE_APP_HPP__
#define __SPYSCOPE_APP_HPP__

#include <wx/wx.h>
#include "../spymap/spymap.hpp"

// Forward-declare spycat types — full definitions included in main.cpp
namespace spycat
{
    class DataSource;
    class SpyNavigator;
    class SpyPlot;
    class SpyWatch;
}

class SpyScope : public wxApp
{
public:
    bool OnInit() override;

    spycat::Spymap*     GetSpymap()     { return map_; }
    spycat::DataSource* GetDataSource() { return source_; }

private:
    spycat::Spymap*       map_     = nullptr;
    spycat::DataSource*   source_  = nullptr;
    spycat::SpyNavigator* nav_     = nullptr;
    spycat::SpyPlot*      plot_    = nullptr;
    spycat::SpyWatch*     watch_   = nullptr;
};

#endif // __SPYSCOPE_APP_HPP__
