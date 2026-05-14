// dockpanel.hpp
#ifndef __SPYCAT_DOCKPANEL_H__
#define __SPYCAT_DOCKPANEL_H__

#include <wx/wx.h>
#include "app.hpp"
#include <wx/aui/aui.h>

namespace spycat
{

class DockPanel : public wxPanel
{
public:
    DockPanel(wxWindow *parent, App& app);
    virtual ~DockPanel();
    wxAuiManager& GetDock() { return dock_; }
private:
    wxAuiManager dock_;
};

} // namespace spycat

#endif // __SPYCAT_DOCKPANEL_H__
