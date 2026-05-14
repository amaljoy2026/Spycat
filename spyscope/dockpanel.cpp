// dockpanel.cpp
#include "dockpanel.hpp"

#include <wx/aui/aui.h>

using namespace spycat;

DockPanel::DockPanel(wxWindow *parent, App& app)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(1024, 768))
{
    dock_.SetManagedWindow(this);
    dock_.GetArtProvider()->SetFont  (wxAUI_DOCKART_CAPTION_FONT,      app.GetTheme().GetFont());
    dock_.GetArtProvider()->SetColour(wxAUI_DOCKART_SASH_COLOUR,       app.GetTheme().GetSecondaryBackgroundColor());
    dock_.GetArtProvider()->SetColour(wxAUI_DOCKART_BACKGROUND_COLOUR, app.GetTheme().GetSecondaryBackgroundColor());
}

DockPanel::~DockPanel()
{
    dock_.UnInit();
}