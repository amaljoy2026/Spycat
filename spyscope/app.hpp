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
    class MainFrame;
}


namespace spycat
{

class App : public wxApp
{
public:
    bool OnInit() override;
    DataSource* GetDataSource() { return source_; }
    Theme& GetTheme() { return theme_; }
    MainFrame *GetMainFrame() { return frame_; }
private:
    DataSource*   source_  = nullptr;
    Theme         theme_;
    MainFrame     *frame_;
};

}

#endif // __SPYSCOPE_APP_HPP__
