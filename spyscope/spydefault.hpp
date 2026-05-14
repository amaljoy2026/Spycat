// spydefault.hpp
#ifndef __SPYSCOPE_SPYDEFAULT_HPP__
#define __SPYSCOPE_SPYDEFAULT_HPP__

#include <wx/wx.h>
#include <wx/aui/aui.h>
#include "app.hpp"

namespace spycat {

class SpyDefault : public wxPanel
{
public:
    SpyDefault(wxWindow* parent, App& app, wxWindowID id = wxID_ANY);

    // Called by the drop target — promotes this panel to a SpyPlot
    void Promote(const std::string& key);

    // Destroys the child plot and returns to the drop-zone state
    void Reset();

private:
    void OnPaint(wxPaintEvent&);
    void OnPaneClose(wxAuiManagerEvent&);

    App&  app_;
    bool  promoted_ = false;
};

} // namespace spycat

#endif // __SPYSCOPE_SPYDEFAULT_HPP__
