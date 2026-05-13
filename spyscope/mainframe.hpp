// mainframe.hpp
#ifndef __SPYCAT_MAINFRAME_H__
#define __SPYCAT_MAINFRAME_H__

#include <wx/wx.h>
#include "app.hpp"
#include <wx/aui/aui.h>

namespace spycat
{

class MainFrame : public wxFrame
{
public:
    MainFrame(const wxString& title, App& app);
    virtual ~MainFrame();
    wxAuiManager& GetDock() { return dock_; }
private:
    wxAuiManager dock_;
};

} // namespace spycat

#endif // __SPYCAT_MAINFRAME_H__
