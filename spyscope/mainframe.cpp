#include "mainframe.hpp"
#include "spynav.hpp"

using namespace spycat;

MainFrame::MainFrame(const wxString& title)
    : wxFrame(NULL, wxID_ANY, title, wxDefaultPosition, wxSize(800, 600))
{
    // 1. Tell the manager to control this top-level frame window
    dock_.SetManagedWindow(this);

    // 4. Force calculation and visually layout the newly configured panels
    dock_.Update();
}

MainFrame::~MainFrame() {
    // Release and cleanly destroy layout rules to prevent memory corruption leaks
    dock_.UnInit();
}