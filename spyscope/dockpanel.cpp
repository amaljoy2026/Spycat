// dockpanel.cpp
#include "dockpanel.hpp"

#include <wx/aui/aui.h>

using namespace spycat;

DockPanel::DockPanel(wxWindow *parent, App& app)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(1024, 768))
{
    dock_.SetManagedWindow(this);
}

DockPanel::~DockPanel()
{
    dock_.UnInit();
}