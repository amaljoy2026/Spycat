// mainframe.cpp
#include "mainframe.hpp"

#include <wx/aui/aui.h>

using namespace spycat;

MainFrame::MainFrame(const wxString& title, App& app)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(1024, 768))
{
    dock_.SetManagedWindow(this);
}

MainFrame::~MainFrame()
{
    dock_.UnInit();
}