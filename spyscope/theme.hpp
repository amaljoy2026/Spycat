#ifndef __SPYSCOPE_THEME__
#define __SPYSCOPE_THEME__

#include <wx/wx.h>

namespace spycat
{

class Theme
{
public:
    wxColour GetHighlightColor() { return p_highlight_; }
    wxColour GetPrimaryBackgroundColor() { return p_background_; }
    wxColour GetSecondaryBackgroundColor() { return s_background_; }
    wxColour GetAltBackgroundColor() { return t_background_; }
    wxColour GetPrimaryTextColor() { return p_text_; }
    wxColour GetHighlightTextColor() { return h_text_; }
    wxColour GetGridColor() { return grid_color_; }
    wxColour GetAxisColor() { return axis_color_; }
    wxFont GetFont() { return p_font_; }
    wxFont GetBoldFont() { return b_font_; }

private:
    wxColour p_highlight_{0x99e550};
    wxColour p_background_{0x2d2d2d};
    wxColour s_background_{0x3e3e3e};
    wxColour t_background_{0x6a775b};
    wxColour p_text_{0xFFFFFF};
    wxColour h_text_{0x000000};
    wxColour grid_color_{0x003300};
    wxColour axis_color_{0x006600};

    wxFont p_font_{14, wxFONTFAMILY_TELETYPE,
            wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL};
    wxFont b_font_{14, wxFONTFAMILY_TELETYPE,
            wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD};
};

}

#endif // __SPYSCOPE_THEME__