#ifndef __SPYCAT_MAINFRAME_H__
#define __SPYCAT_MAINFRAME_H__

#include <wx/wx.h>
#include <wx/aui/aui.h>
#include "app.hpp"

namespace spycat
{

class MainFrame : public wxFrame 
{
public:
    MainFrame(const wxString& title);
    virtual ~MainFrame();
    wxAuiManager& GetDock() { return dock_; }
private:
    // The core controller managing all dockable components
    wxAuiManager dock_;
};

}

#endif // __SPYCAT_MAINFRAME_H__