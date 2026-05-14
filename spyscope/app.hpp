// app.hpp
#ifndef __SPYSCOPE_APP_HPP__
#define __SPYSCOPE_APP_HPP__

#include <wx/wx.h>
#include <vector>
#include <algorithm>
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

// ── DataObserver ──────────────────────────────────────────────────────────────
//
// Any panel that wants to receive data updates from the master polling timer
// should inherit this interface and call App::RegisterObserver in its
// constructor and App::UnregisterObserver in its destructor.
class DataObserver
{
public:
    virtual ~DataObserver() = default;
    virtual void OnDataPoll() = 0;
};

class App : public wxApp
{
public:
    bool OnInit() override;
    DataSource* GetDataSource() { return source_; }
    Theme& GetTheme() { return theme_; }
    DockPanel *GetDockPanel() { return dockpanel_; }
    SpyWatch*  GetSpyWatch()  { return watch_; }

    // Dynamically add a new plot or watch pane to the right dock
    void AddPlotPane(const std::vector<std::string>& keys);
    void AddWatchPane(const std::vector<std::string>& keys);

    // Single-key convenience wrappers
    void AddPlotPane(const std::string& key) { AddPlotPane(std::vector<std::string>{key}); }
    void AddWatchPane(const std::string& key) { AddWatchPane(std::vector<std::string>{key}); }

    // Observer registration — panels call these in their constructor/destructor
    void RegisterObserver(DataObserver* observer)
    {
        observers_.push_back(observer);
    }
    void UnregisterObserver(DataObserver* observer)
    {
        observers_.erase(
            std::remove(observers_.begin(), observers_.end(), observer),
            observers_.end());
    }

private:
    wxPanel *CreateTopbar();
    void OnDataTimer(wxTimerEvent&);

    DataSource*   source_    = nullptr;
    Theme         theme_;
    DockPanel*    dockpanel_ = nullptr;
    SpyWatch*     watch_     = nullptr;
    wxPanel*      topbar_    = nullptr;
    wxFrame*      frame_     = nullptr;
    wxSizer*      sizer_     = nullptr;

    std::vector<DataObserver*> observers_;
    wxTimer                    data_timer_;
};

}

#endif // __SPYSCOPE_APP_HPP__
