// spysettings.hpp
#ifndef __SPYSCOPE_SPYSETTINGS_HPP__
#define __SPYSCOPE_SPYSETTINGS_HPP__

#include <wx/wx.h>
#include "app.hpp"

namespace spycat
{

class SpySettings : public wxDialog
{
public:
    explicit SpySettings(wxWindow* parent, App& app);

    // Returns the (possibly edited) segment name; call after ShowModal() == wxID_OK
    wxString GetSegmentName() const;

private:
    App&        app_;
    wxTextCtrl* name_ctrl_;
};

} // namespace spycat
#endif // __SPYSCOPE_SPYSETTINGS_HPP__
