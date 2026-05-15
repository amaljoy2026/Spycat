// app.cpp
#include "app.hpp"
#include "dockpanel.hpp"
#include "datasource.hpp"
#include "spynav.hpp"
#include "spyplot.hpp"
#include "spywatch.hpp"
#include "spydefault.hpp"
#include "spysettings.hpp"
#include "logo.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace pt = boost::property_tree;

using namespace spycat;

wxPanel *App::CreateTopbar()
{
    wxPanel *topbar = new wxPanel(frame_, wxID_ANY, {0, 0}, {-1, 42}, wxBORDER_NONE, wxEmptyString);
    topbar->SetBackgroundColour(GetTheme().GetSecondaryBackgroundColor());
    wxStaticBitmap *logo = new wxStaticBitmap(topbar, wxID_ANY, GetSpyCatLogoBitmap(), {0, 0}, {32, 32});
    
    wxStaticText *title = new wxStaticText(topbar, wxID_ANY, "Spycat");
    title->SetFont(GetTheme().GetTitleFont());
    title->SetForegroundColour(GetTheme().GetPrimaryTextColor());

    // Settings button — top-right corner
    auto* settings_btn = new wxButton(topbar, wxID_ANY, "⚙",
                                      wxDefaultPosition, wxSize(32, 32),
                                      wxBORDER_NONE);
    settings_btn->SetFont(wxFont(30, wxFONTFAMILY_DEFAULT,
                                 wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    settings_btn->SetForegroundColour(GetTheme().GetPrimaryTextColor());
    settings_btn->SetBackgroundColour(GetTheme().GetSecondaryBackgroundColor());
    settings_btn->SetToolTip("Settings");

    settings_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        SpySettings dlg(frame_, *this);
        dlg.CentreOnParent();
        if (dlg.ShowModal() != wxID_OK) return;

        wxString new_name = dlg.GetSegmentName();
        if (new_name.empty()) return;
        // No sameness check — the shared memory segment may have been recreated
        // underneath us, so reconnecting to the same name is intentional.

        // Swap to new segment immediately so observers see the new source.
        // Destroy the old DataSource deferred — lets any queued timer events
        // from it drain out of the event queue before the object is freed.
        segment_name_ = new_name.ToStdString();
        SaveLayout();
        DataSource* old = source_;
        source_ = new DataSource(segment_name_);
        CallAfter([old]() { delete old; });
    });

    wxSizer *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(logo,         0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
    sizer->Add(title,        0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
    sizer->AddStretchSpacer(1);
    sizer->Add(settings_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    topbar->SetSizer(sizer);
    return topbar;
}

bool App::OnInit()
{
    wxInitAllImageHandlers(); // <-- add this line

    // Pre-read segment_name_ from layout.json before opening shared memory.
    // The full LoadLayout() is deferred via CallAfter (needs a live frame),
    // but we need the correct segment name right now.
    int win_w = 1024, win_h = 768;
    if (wxFileExists(DefaultLayoutPath())) {
        try {
            pt::ptree root;
            pt::read_json(DefaultLayoutPath().ToStdString(), root);
            segment_name_ = root.get<std::string>("segment_name", segment_name_);
            win_w = root.get<int>("window_width",  win_w);
            win_h = root.get<int>("window_height", win_h);
        } catch (...) {}
    }

    source_ = new DataSource(segment_name_);

    frame_ = new wxFrame(nullptr, wxID_ANY, "Spycat", wxDefaultPosition, wxSize(win_w, win_h));
    frame_->SetBackgroundColour(GetTheme().GetSecondaryBackgroundColor());
    
    topbar_ = CreateTopbar();

    dockpanel_ = new DockPanel(frame_, *this);
    dockpanel_->SetBackgroundColour(GetTheme().GetSecondaryBackgroundColor());

    sizer_ = new wxBoxSizer(wxVERTICAL);
    sizer_->Add(topbar_, 0, wxEXPAND);
    sizer_->Add(dockpanel_, 1, wxEXPAND);
    frame_->SetSizer(sizer_);
    frame_->SetAutoLayout(true);


    wxAuiManager& dock = dockpanel_->GetDock();

    watch_ = new SpyWatch(dockpanel_, *this);
    dock.AddPane(watch_, wxAuiPaneInfo()
        .Name("watch_main")
        .Caption("Watch")
        .CloseButton(true)
        .MinSize({-1, 220})
        .Bottom()
    );
    dock.AddPane(new SpyNav(dockpanel_, *this), wxAuiPaneInfo()
        .Name("nav")
        .Caption("Navigator")
        .CloseButton(false)
        .MinSize({180, -1})
        .Left()
        .Layer(1)
    );
    dock.AddPane(new SpyDefault(dockpanel_, *this), wxAuiPaneInfo()
        .Name("center")
        .Caption("")
        .CloseButton(true)
        .Center()
    );

    dock.Update();
    frame_->Show();

    // Auto-save on close
    frame_->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
        // Release all overrides before saving so the producer regains control
        watch_->ReleaseOverrides();
        for (auto& rec : panes_)
            if (rec.type == "watch")
                if (auto* w = dynamic_cast<SpyWatch*>(rec.widget))
                    w->ReleaseOverrides();

        SaveLayout();
        e.Skip();   // let wxWidgets destroy the frame
    });

    // Master data poll timer — all registered DataObservers are notified each tick
    data_timer_.Bind(wxEVT_TIMER, &App::OnDataTimer, this);
    data_timer_.Start(15);

    // Auto-restore layout from previous session (deferred so frame has settled)
    if (wxFileExists(DefaultLayoutPath()))
        CallAfter([this]() { LoadLayout(); });

    return true;
}

void App::OnDataTimer(wxTimerEvent&)
{
    // Single tick: refresh the cache from shared memory, then fan out to all
    // observers.  DataSource no longer has its own timer.
    if (source_) source_->Poll();

    for (auto* obs : observers_)
        obs->OnDataPoll();
}

void App::AddPlotPane(const std::vector<std::string>& keys)
{
    if (keys.empty()) return;
    wxAuiManager& dock = dockpanel_->GetDock();

    wxString caption;
    if (keys.size() == 1) {
        caption = wxString::FromUTF8(keys[0]);
        int dot = caption.Find('.', /*fromEnd=*/true);
        if (dot != wxNOT_FOUND) caption = caption.Mid(dot + 1);
    } else {
        caption = "Plot";
    }

    wxString pane_name = wxString::Format("plot_%d", pane_counter_++);

    SpyPlot* plot = new SpyPlot(dockpanel_, *this, keys[0]);
    for (size_t i = 1; i < keys.size(); ++i)
        plot->AddTrace(keys[i]);

    dock.AddPane(plot, wxAuiPaneInfo()
        .Name(pane_name)
        .Caption(caption)
        .CloseButton(true)
        .Right()
        .MinSize({200, 150})
        .BestSize({300, 200})
        .Layer(1)
    );
    dock.Update();

    panes_.push_back({ pane_name.ToStdString(), "plot", plot });
}

void App::AddWatchPane(const std::vector<std::string>& keys)
{
    if (keys.empty()) return;
    wxAuiManager& dock = dockpanel_->GetDock();

    wxString caption;
    if (keys.size() == 1) {
        caption = wxString::FromUTF8(keys[0]);
        int dot = caption.Find('.', /*fromEnd=*/true);
        if (dot != wxNOT_FOUND) caption = caption.Mid(dot + 1);
    } else {
        caption = "Watch";
    }

    wxString pane_name = wxString::Format("watch_%d", pane_counter_++);

    SpyWatch* watch = new SpyWatch(dockpanel_, *this);
    for (const auto& key : keys)
        watch->AddKey(key);

    dock.AddPane(watch, wxAuiPaneInfo()
        .Name(pane_name)
        .Caption(caption)
        .CloseButton(true)
        .Right()
        .MinSize({250, 80})
        .BestSize({350, 120})
        .Layer(1)
    );
    dock.Update();

    panes_.push_back({ pane_name.ToStdString(), "watch", watch });
}

// ── Layout + settings persistence ────────────────────────────────────────────

wxString App::DefaultLayoutPath() const
{
    // Save layout.json next to the executable for easy portability.
    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    return exe.GetPath() + wxFileName::GetPathSeparator() + "layout.json";
}

void App::SaveLayout(const wxString& path)
{
    const wxString save_path = path.empty() ? DefaultLayoutPath() : path;
    wxAuiManager& dock = dockpanel_->GetDock();

    try {
        pt::ptree root;
        root.put("version", 1);
        root.put("segment_name", segment_name_);
        root.put("window_width",  frame_->GetSize().x);
        root.put("window_height", frame_->GetSize().y);
        root.put("perspective", dock.SavePerspective().ToStdString());

        pt::ptree panes_node;

        // ── Main watch ───────────────────────────────────────────────────────
        {
            pt::ptree node;
            node.put("name",    "watch_main");
            node.put("type",    "watch");
            node.put("caption", dock.GetPane(watch_).caption.ToStdString());
            watch_->SerializeTo(node);
            panes_node.push_back({"", node});
        }

        // ── Center pane — serialize if a plot has been promoted into it ─────
        {
            wxAuiPaneInfo& ci = dock.GetPane("center");
            if (ci.IsOk()) {
                if (auto* def = dynamic_cast<SpyDefault*>(ci.window)) {
                    if (def->IsPromoted()) {
                        pt::ptree node;
                        node.put("name",    "center");
                        node.put("type",    "center_plot");
                        node.put("caption", ci.caption.ToStdString());
                        def->SerializeTo(node);
                        panes_node.push_back({"", node});
                    }
                }
            }
        }

        // ── Dynamic panes ────────────────────────────────────────────────────
        for (const auto& rec : panes_) {
            pt::ptree node;
            node.put("name", rec.name);
            node.put("type", rec.type);

            wxAuiPaneInfo& info = dock.GetPane(rec.widget);
            node.put("caption", info.IsOk() ? info.caption.ToStdString() : "");

            if (rec.type == "plot") {
                if (auto* p = dynamic_cast<SpyPlot*>(rec.widget))
                    p->SerializeTo(node);
            } else if (rec.type == "watch") {
                if (auto* w = dynamic_cast<SpyWatch*>(rec.widget))
                    w->SerializeTo(node);
            }

            panes_node.push_back({"", node});
        }

        root.add_child("panes", panes_node);
        pt::write_json(save_path.ToStdString(), root);

    } catch (const std::exception& ex) {
        wxLogWarning("SaveLayout failed: %s", ex.what());
    }
}

void App::LoadLayout(const wxString& path)
{
    const wxString load_path = path.empty() ? DefaultLayoutPath() : path;
    if (!wxFileExists(load_path)) return;

    pt::ptree root;
    try {
        pt::read_json(load_path.ToStdString(), root);
    } catch (const std::exception& ex) {
        wxLogWarning("LoadLayout: could not parse %s — %s",
                     load_path.ToStdString(), ex.what());
        return;
    }

    segment_name_ = root.get<std::string>("segment_name", segment_name_);

    wxAuiManager& dock = dockpanel_->GetDock();

    // ── Destroy all existing dynamic panes ───────────────────────────────────
    for (auto& rec : panes_) {
        dock.DetachPane(rec.widget);
        rec.widget->Destroy();
    }
    panes_.clear();
    pane_counter_ = 0;

    // ── Recreate panes from JSON ──────────────────────────────────────────────
    pt::ptree empty_panes;
    for (const auto& entry : root.get_child("panes", empty_panes)) {
        const pt::ptree& node    = entry.second;
        std::string      name    = node.get<std::string>("name",    "");
        std::string      type    = node.get<std::string>("type",    "");
        std::string      caption = node.get<std::string>("caption", "");

        if (name == "watch_main") {
            // Restore content of the always-present main Watch pane
            watch_->DeserializeFrom(node);

        } else if (type == "plot") {
            SpyPlot* plot = new SpyPlot(dockpanel_, *this);
            plot->DeserializeFrom(node);

            dock.AddPane(plot, wxAuiPaneInfo()
                .Name(wxString::FromUTF8(name))
                .Caption(wxString::FromUTF8(caption))
                .CloseButton(true)
                .Right()
                .MinSize({200, 150})
                .BestSize({300, 200})
                .Layer(1)
            );
            panes_.push_back({ name, "plot", plot });
            ++pane_counter_;

        } else if (type == "center_plot") {
            // Restore a promoted plot inside the persistent center SpyDefault pane
            wxAuiPaneInfo& ci = dock.GetPane("center");
            if (ci.IsOk()) {
                if (auto* def = dynamic_cast<SpyDefault*>(ci.window))
                    def->DeserializeFrom(node);
            }

        } else if (type == "watch") {
            SpyWatch* watch = new SpyWatch(dockpanel_, *this);
            watch->DeserializeFrom(node);

            dock.AddPane(watch, wxAuiPaneInfo()
                .Name(wxString::FromUTF8(name))
                .Caption(wxString::FromUTF8(caption))
                .CloseButton(true)
                .Right()
                .MinSize({250, 80})
                .BestSize({350, 120})
                .Layer(1)
            );
            panes_.push_back({ name, "watch", watch });
            ++pane_counter_;
        }
    }

    // ── Restore AUI geometry — must happen after all panes are registered ─────
    std::string perspective = root.get<std::string>("perspective", "");
    if (!perspective.empty())
        dock.LoadPerspective(wxString::FromUTF8(perspective), /*update=*/false);

    dock.Update();
}

wxIMPLEMENT_APP(App);
