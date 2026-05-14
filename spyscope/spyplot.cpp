// spyplot.cpp
#include "spyplot.hpp"

#include <wx/dcbuffer.h>
#include <wx/dnd.h>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <variant>
#include <unordered_set>
#include <limits>
#include <boost/property_tree/ptree.hpp>

#include "theme.hpp"
#include "dockpanel.hpp"

namespace spycat
{

// ── Trace colour palette ──────────────────────────────────────────────────────

static const wxColour kTracePalette[] = {
    { 0x00, 0xCC, 0x66 },   // green
    { 0x00, 0xCC, 0xFF },   // cyan
    { 0xFF, 0xD7, 0x00 },   // amber
    { 0xFF, 0x77, 0x00 },   // orange
    { 0xFF, 0x44, 0xFF },   // magenta
    { 0xFF, 0x44, 0x44 },   // red
    { 0x88, 0xFF, 0x00 },   // lime
    { 0xAA, 0x88, 0xFF },   // lavender
};
static constexpr size_t kPaletteSize = sizeof(kTracePalette) / sizeof(kTracePalette[0]);

// ── Marker geometry constants ─────────────────────────────────────────────────
static constexpr double MARKER_SNAP_PX  = 12.0;  // creation threshold (pixels)
static constexpr double MARKER_HIT_PX   =  8.0;  // drag hit radius    (pixels)
static constexpr double MARKER_RADIUS   =  5.0;  // circle radius      (pixels)
static constexpr int    MARKER_CTRL_W   = 120;   // text control width
static constexpr int    MARKER_CTRL_H   = 22;    // text control height
static constexpr int    MARKER_CTRL_GAP =  2;    // gap between the two controls

// ── Drop target ───────────────────────────────────────────────────────────────

class PlotDropTarget : public wxTextDropTarget
{
public:
    PlotDropTarget(SpyPlot* plot) : plot_(plot) {}

    bool OnDropText(wxCoord, wxCoord, const wxString& text) override
    {
        for (const auto& key : wxSplit(text, '\n'))
            if (!key.IsEmpty()) plot_->AddTrace(key.ToStdString());
        return true;
    }

private:
    SpyPlot* plot_;
};

// ── Construction ──────────────────────────────────────────────────────────────
SpyPlot::SpyPlot(wxWindow* parent, App& app, const std::string& key,
                 wxWindowID id, const wxPoint& pos,
                 const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
    , app_(app)
    , paint_timer_(this)
{
    traces_.emplace_back(key, kTracePalette[color_counter_++ % kPaletteSize]);
    per_trace_lo_.push_back(-1.0);
    per_trace_hi_.push_back( 1.0);

    SetBackgroundStyle(wxBG_STYLE_PAINT);   // required for wxAutoBufferedPaintDC
    SetBackgroundColour(app_.GetTheme().GetPrimaryBackgroundColor());
    SetMinSize({ 200, 100 });

    Bind(wxEVT_PAINT,         &SpyPlot::OnPaint,       this);
    Bind(wxEVT_SIZE,          &SpyPlot::OnSize,         this);
    Bind(wxEVT_TIMER,         &SpyPlot::OnPaintTimer,   this, paint_timer_.GetId());
    Bind(wxEVT_CONTEXT_MENU,  &SpyPlot::OnContextMenu,  this);
    Bind(wxEVT_MOUSEWHEEL,    &SpyPlot::OnMouseWheel,   this);
    Bind(wxEVT_LEFT_DOWN,     &SpyPlot::OnLeftDown,     this);
    Bind(wxEVT_LEFT_UP,       &SpyPlot::OnLeftUp,       this);
    Bind(wxEVT_LEFT_DCLICK,   &SpyPlot::OnLeftDClick,   this);
    Bind(wxEVT_MOTION,        &SpyPlot::OnMouseMotion,  this);
    Bind(wxEVT_KEY_DOWN,      &SpyPlot::OnKeyDown,      this);

    paint_timer_.Start(16);   // ~60 Hz repaint

    app_.RegisterObserver(this);
    SetDropTarget(new PlotDropTarget(this));
}

SpyPlot::~SpyPlot()
{
    app_.UnregisterObserver(this);
}

// ── Public API ────────────────────────────────────────────────────────────────
void SpyPlot::SetKey(const std::string& key)
{
    traces_.clear();
    color_counter_ = 0;
    traces_.emplace_back(key, kTracePalette[color_counter_++ % kPaletteSize]);
    per_trace_lo_ = { -1.0 };
    per_trace_hi_ = {  1.0 };
    selected_trace_idx_ = 0;
}

void SpyPlot::AddTrace(const std::string& key)
{
    for (const auto& t : traces_)
        if (t.key == key) return;

    traces_.emplace_back(key, kTracePalette[color_counter_++ % kPaletteSize]);
    per_trace_lo_.push_back(-1.0);
    per_trace_hi_.push_back( 1.0);

    // On second trace, rename the AUI pane to the generic "Plot"
    if (traces_.size() == 2) {
        wxAuiManager& dock = app_.GetDockPanel()->GetDock();
        wxAuiPaneInfo& pane = dock.GetPane(this);
        if (pane.IsOk()) {
            pane.Caption("Plot");
            dock.Update();
        } else {
            // Embedded inside SpyDefault — pane is the parent
            wxAuiPaneInfo& parent_pane = dock.GetPane(GetParent());
            if (parent_pane.IsOk()) {
                parent_pane.Caption("Plot");
                dock.Update();
            }
        }
    }
}

void SpyPlot::PushSample(double value)
{
    if (traces_.empty()) return;
    double t = std::chrono::duration<double>(Clock::now() - start_).count();
    traces_[0].data.push_back({ t, value });
}

// ── Marker helpers ────────────────────────────────────────────────────────────

void SpyPlot::ClearMarkers()
{
    for (auto& link : links_) {
        link.dy_ctrl->Destroy();
        link.dx_ctrl->Destroy();
    }
    links_.clear();

    for (auto& m : markers_) {
        m.x_ctrl->Destroy();
        m.y_ctrl->Destroy();
    }
    markers_.clear();
    dragging_marker_       = -1;
    shift_selected_marker_ = -1;
}

void SpyPlot::OnLeftDClick(wxMouseEvent& e)
{
    if (!paused_) { e.Skip(); return; }

    wxPoint pos = e.GetPosition();

    // Find the visible sample closest to the click, across all traces
    double t_right = frozen_time_s_ + pan_offset_s_;
    double t_left  = t_right - time_window_s_;

    double best_dist  = std::numeric_limits<double>::max();
    size_t best_trace = 0;
    double best_time  = 0.0;
    double best_value = 0.0;

    for (size_t ti = 0; ti < traces_.size(); ++ti) {
        for (const auto& s : traces_[ti].data) {
            if (s.time_s < t_left) continue;
            double dx   = pos.x - ClientX(s.time_s);
            double dy   = pos.y - ClientY(ti, s.value);
            double dist = std::sqrt(dx*dx + dy*dy);
            if (dist < best_dist) {
                best_dist  = dist;
                best_trace = ti;
                best_time  = s.time_s;
                best_value = s.value;
            }
        }
    }

    if (best_dist > MARKER_SNAP_PX) return;   // not close enough to any trace

    Marker m;
    m.trace_idx = best_trace;
    m.time_s    = best_time;
    m.value     = best_value;

    const wxColour& col = traces_[best_trace].colour;

    // y_ctrl — value label (top), updates on drag
    std::ostringstream yss;
    yss << std::setprecision(6) << best_value;
    m.y_ctrl = new wxStaticText(this, wxID_ANY,
                                 wxString::FromUTF8("y: " + yss.str()),
                                 wxDefaultPosition, wxSize(MARKER_CTRL_W, MARKER_CTRL_H),
                                 wxST_NO_AUTORESIZE | wxALIGN_LEFT);
    m.y_ctrl->SetFont(app_.GetTheme().GetFont());
    m.y_ctrl->SetForegroundColour(*wxWHITE);
    m.y_ctrl->SetBackgroundColour(app_.GetTheme().GetAltBackgroundColor());

    // x_ctrl — time label (below y_ctrl), updates on drag
    std::ostringstream xss;
    xss << std::fixed << std::setprecision(3) << best_time << "s";
    m.x_ctrl = new wxStaticText(this, wxID_ANY,
                                 wxString::FromUTF8("x: " + xss.str()),
                                 wxDefaultPosition, wxSize(MARKER_CTRL_W, MARKER_CTRL_H),
                                 wxST_NO_AUTORESIZE | wxALIGN_LEFT);
    m.x_ctrl->SetFont(app_.GetTheme().GetFont());
    m.x_ctrl->SetForegroundColour(*wxWHITE);
    m.x_ctrl->SetBackgroundColour(app_.GetTheme().GetPrimaryBackgroundColor());

    markers_.push_back(m);
    RepositionMarkerControls();
    Refresh();
}

void SpyPlot::DrawMarkers(wxGraphicsContext* gc)
{
    for (size_t i = 0; i < markers_.size(); ++i) {
        if (markers_[i].trace_idx >= traces_.size()) continue;
        double cx = ClientX(markers_[i].time_s);
        double cy = ClientY(markers_[i].trace_idx, markers_[i].value);

        // Highlight ring when this marker is the first shift-selected endpoint
        if (static_cast<int>(i) == shift_selected_marker_) {
            gc->SetPen(wxPen(*wxWHITE, 2));
            gc->SetBrush(*wxTRANSPARENT_BRUSH);
            gc->DrawEllipse(cx - MARKER_RADIUS - 3, cy - MARKER_RADIUS - 3,
                            2 * (MARKER_RADIUS + 3), 2 * (MARKER_RADIUS + 3));
        }

        gc->SetPen(wxPen(*wxWHITE, 1));
        gc->SetBrush(wxBrush(traces_[markers_[i].trace_idx].colour));
        gc->DrawEllipse(cx - MARKER_RADIUS, cy - MARKER_RADIUS,
                        2 * MARKER_RADIUS,  2 * MARKER_RADIUS);
    }
}

void SpyPlot::RepositionMarkerControls()
{
    wxSize sz = GetSize();

    // ── Per-marker controls ───────────────────────────────────────────────────
    for (auto& m : markers_) {
        int cx = static_cast<int>(ClientX(m.time_s));
        int cy = static_cast<int>(ClientY(m.trace_idx, m.value));

        // x_ctrl sits immediately above the circle; y_ctrl is above x_ctrl
        int x_top = cy - static_cast<int>(MARKER_RADIUS) - MARKER_CTRL_GAP - MARKER_CTRL_H;
        int y_top = x_top - MARKER_CTRL_GAP - MARKER_CTRL_H;

        int left = cx - MARKER_CTRL_W / 2;
        left = std::max(MARGIN_L, std::min(left, sz.x - MARGIN_R - MARKER_CTRL_W));

        m.x_ctrl->SetSize(left, x_top, MARKER_CTRL_W, MARKER_CTRL_H);
        m.y_ctrl->SetSize(left, y_top, MARKER_CTRL_W, MARKER_CTRL_H);
    }

    // ── Link midpoint controls ────────────────────────────────────────────────
    for (auto& link : links_) {
        if (link.from_idx >= markers_.size() || link.to_idx >= markers_.size()) continue;

        const Marker& from = markers_[link.from_idx];
        const Marker& to   = markers_[link.to_idx];

        // Midpoint in screen coords
        int cx = static_cast<int>((ClientX(from.time_s) + ClientX(to.time_s)) / 2.0);
        int cy = static_cast<int>((ClientY(from.trace_idx, from.value) + ClientY(to.trace_idx, to.value)) / 2.0);

        // Stack labels above the midpoint dot (same convention as marker controls)
        int dx_top = cy - MARKER_CTRL_GAP - MARKER_CTRL_H;
        int dy_top = dx_top - MARKER_CTRL_GAP - MARKER_CTRL_H;

        int left = cx - MARKER_CTRL_W / 2;
        left = std::max(MARGIN_L, std::min(left, sz.x - MARGIN_R - MARKER_CTRL_W));

        link.dy_ctrl->SetSize(left, dy_top, MARKER_CTRL_W, MARKER_CTRL_H);
        link.dx_ctrl->SetSize(left, dx_top, MARKER_CTRL_W, MARKER_CTRL_H);

        // Refresh Δ values (markers may have been dragged)
        double dt = to.time_s - from.time_s;
        double dv = to.value  - from.value;

        std::ostringstream yss;
        yss << std::showpos << std::setprecision(4) << dv;
        link.dy_ctrl->SetLabel(wxString::FromUTF8("Δy: " + yss.str()));

        std::ostringstream xss;
        xss << std::showpos << std::fixed << std::setprecision(3) << dt << "s";
        link.dx_ctrl->SetLabel(wxString::FromUTF8("Δx: " + xss.str()));
    }
}

void SpyPlot::OnKeyDown(wxKeyEvent& e)
{
    if (e.GetKeyCode() == WXK_ESCAPE && shift_selected_marker_ >= 0) {
        shift_selected_marker_ = -1;
        Refresh();
        return;
    }
    e.Skip();
}

// ── Marker links ─────────────────────────────────────────────────────────────

void SpyPlot::CreateMarkerLink(int from_idx, int to_idx)
{
    const Marker& from = markers_[from_idx];
    const Marker& to   = markers_[to_idx];

    double dt = to.time_s - from.time_s;
    double dv = to.value  - from.value;

    MarkerLink link;
    link.from_idx = static_cast<size_t>(from_idx);
    link.to_idx   = static_cast<size_t>(to_idx);

    // dy_ctrl — Δy label (top)
    std::ostringstream yss;
    yss << std::showpos << std::setprecision(4) << dv;
    link.dy_ctrl = new wxStaticText(this, wxID_ANY,
                                     wxString::FromUTF8("Δy: " + yss.str()),
                                     wxDefaultPosition, wxSize(MARKER_CTRL_W, MARKER_CTRL_H),
                                     wxST_NO_AUTORESIZE | wxALIGN_LEFT);
    link.dy_ctrl->SetFont(app_.GetTheme().GetFont());
    link.dy_ctrl->SetForegroundColour(*wxWHITE);
    link.dy_ctrl->SetBackgroundColour(app_.GetTheme().GetAltBackgroundColor());

    // dx_ctrl — Δx label (below dy)
    std::ostringstream xss;
    xss << std::showpos << std::fixed << std::setprecision(3) << dt << "s";
    link.dx_ctrl = new wxStaticText(this, wxID_ANY,
                                     wxString::FromUTF8("Δx: " + xss.str()),
                                     wxDefaultPosition, wxSize(MARKER_CTRL_W, MARKER_CTRL_H),
                                     wxST_NO_AUTORESIZE | wxALIGN_LEFT);
    link.dx_ctrl->SetFont(app_.GetTheme().GetFont());
    link.dx_ctrl->SetForegroundColour(*wxWHITE);
    link.dx_ctrl->SetBackgroundColour(app_.GetTheme().GetPrimaryBackgroundColor());

    links_.push_back(link);
    RepositionMarkerControls();
    Refresh();
}

void SpyPlot::DrawMarkerLinks(wxGraphicsContext* gc)
{
    for (const auto& link : links_) {
        if (link.from_idx >= markers_.size() || link.to_idx >= markers_.size()) continue;

        const Marker& from = markers_[link.from_idx];
        const Marker& to   = markers_[link.to_idx];

        double x1 = ClientX(from.time_s), y1 = ClientY(from.trace_idx, from.value);
        double x2 = ClientX(to.time_s),   y2 = ClientY(to.trace_idx,  to.value);

        // Dashed white line
        gc->SetPen(gc->CreatePen(
            wxGraphicsPenInfo(*wxWHITE, 1.0, wxPENSTYLE_SHORT_DASH)));
        gc->StrokeLine(x1, y1, x2, y2);

        // Small dot at midpoint as visual anchor for the labels
        double mx = (x1 + x2) / 2.0;
        double my = (y1 + y2) / 2.0;
        gc->SetPen(wxPen(*wxWHITE, 1));
        gc->SetBrush(wxBrush(*wxWHITE));
        gc->DrawEllipse(mx - 3, my - 3, 6, 6);
    }
}

// ── Data poll (called by App's master timer) ──────────────────────────────────
void SpyPlot::OnDataPoll()
{
    if (paused_) return;

    DataSource* source = app_.GetDataSource();
    if (!source) return;

    double t = std::chrono::duration<double>(Clock::now() - start_).count();

    for (auto& trace : traces_) {
        auto entry = source->Get(trace.key);
        if (!entry) continue;

        double value = std::visit([](auto&& v) -> double {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_arithmetic_v<T>)
                return static_cast<double>(v);
            return 0.0;
        }, entry->value);

        trace.data.push_back({ t, value });
    }
}

// ── Coordinate transforms ─────────────────────────────────────────────────────
double SpyPlot::ClientX(double time_s) const
{
    double t_now   = paused_ ? frozen_time_s_
                             : std::chrono::duration<double>(Clock::now() - start_).count();
    double t_right = t_now + pan_offset_s_;
    double t_left  = t_right - time_window_s_;
    double plot_w  = GetSize().x - MARGIN_L - MARGIN_R;
    return MARGIN_L + (time_s - t_left) / time_window_s_ * plot_w;
}

double SpyPlot::ClientY(double value) const
{
    double plot_h = GetSize().y - MARGIN_T - MARGIN_B;
    double range  = (y_hi_ == y_lo_) ? 1.0 : (y_hi_ - y_lo_);
    return MARGIN_T + (1.0 - (value - y_lo_) / range) * plot_h;
}

double SpyPlot::ClientY(size_t trace_idx, double value) const
{
    double lo, hi;
    if (!shared_axis_ && trace_idx < per_trace_lo_.size()) {
        lo = per_trace_lo_[trace_idx];
        hi = per_trace_hi_[trace_idx];
    } else {
        lo = y_lo_;
        hi = y_hi_;
    }
    double plot_h = GetSize().y - MARGIN_T - MARGIN_B;
    double range  = (hi == lo) ? 1.0 : (hi - lo);
    return MARGIN_T + (1.0 - (value - lo) / range) * plot_h;
}

double SpyPlot::PlotTime(int px) const
{
    double t_now   = paused_ ? frozen_time_s_
                             : std::chrono::duration<double>(Clock::now() - start_).count();
    double t_right = t_now + pan_offset_s_;
    double t_left  = t_right - time_window_s_;
    double plot_w  = GetSize().x - MARGIN_L - MARGIN_R;
    return t_left + (px - MARGIN_L) / plot_w * time_window_s_;
}

double SpyPlot::PlotValue(int py) const
{
    double plot_h = GetSize().y - MARGIN_T - MARGIN_B;
    double range  = (y_hi_ == y_lo_) ? 1.0 : (y_hi_ - y_lo_);
    return y_lo_ + (1.0 - (py - MARGIN_T) / plot_h) * range;
}

// ── Auto-scale ────────────────────────────────────────────────────────────────
void SpyPlot::UpdateYRange()
{
    double t_now  = paused_ ? frozen_time_s_
                            : std::chrono::duration<double>(Clock::now() - start_).count();
    double t_left = t_now + pan_offset_s_ - time_window_s_;

    if (auto_scale_) {
        if (shared_axis_) {
            // All traces share one Y range
            double lo =  std::numeric_limits<double>::max();
            double hi = -std::numeric_limits<double>::max();
            bool   any = false;

            for (const auto& trace : traces_) {
                for (const auto& s : trace.data) {
                    if (s.time_s < t_left) continue;
                    lo = std::min(lo, s.value);
                    hi = std::max(hi, s.value);
                    any = true;
                }
            }

            if (!any) return;
            if (lo == hi) { lo -= 1.0; hi += 1.0; }
            double margin = (hi - lo) * 0.05;
            y_lo_ = lo - margin;
            y_hi_ = hi + margin;
        } else {
            // Per-trace Y ranges — compute independently
            per_trace_lo_.resize(traces_.size(), -1.0);
            per_trace_hi_.resize(traces_.size(),  1.0);

            for (size_t ti = 0; ti < traces_.size(); ++ti) {
                double lo =  std::numeric_limits<double>::max();
                double hi = -std::numeric_limits<double>::max();
                bool   any = false;

                for (const auto& s : traces_[ti].data) {
                    if (s.time_s < t_left) continue;
                    lo = std::min(lo, s.value);
                    hi = std::max(hi, s.value);
                    any = true;
                }

                if (!any) continue;
                if (lo == hi) { lo -= 1.0; hi += 1.0; }
                double margin = (hi - lo) * 0.05;
                per_trace_lo_[ti] = lo - margin;
                per_trace_hi_[ti] = hi + margin;
            }
        }
    }

    // In normalised mode, y_lo_/y_hi_ must always track the selected trace so
    // axis labels are correct regardless of auto_scale_ state.
    if (!shared_axis_ && !traces_.empty()) {
        size_t si = std::min(selected_trace_idx_, traces_.size() - 1);
        if (si < per_trace_lo_.size()) {
            y_lo_ = per_trace_lo_[si];
            y_hi_ = per_trace_hi_[si];
        }
    }
}

// ── Drawing ───────────────────────────────────────────────────────────────────
void SpyPlot::DrawBackground(wxGraphicsContext* gc)
{
    wxSize sz = GetSize();
    gc->SetBrush(wxBrush(app_.GetTheme().GetPrimaryBackgroundColor()));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawRectangle(0, 0, sz.x, sz.y);
}

void SpyPlot::DrawGrid(wxGraphicsContext* gc)
{
    wxSize sz    = GetSize();
    double plot_w = sz.x - MARGIN_L - MARGIN_R;
    double plot_h = sz.y - MARGIN_T - MARGIN_B;

    // 5 horizontal grid lines
    gc->SetPen(wxPen(app_.GetTheme().GetGridColor(), 1, wxPENSTYLE_DOT));
    const int H_LINES = 5;
    for (int i = 0; i <= H_LINES; ++i) {
        double y = MARGIN_T + i * plot_h / H_LINES;
        gc->StrokeLine(MARGIN_L, y, MARGIN_L + plot_w, y);
    }

    // 5 vertical grid lines
    const int V_LINES = 5;
    for (int i = 0; i <= V_LINES; ++i) {
        double x = MARGIN_L + i * plot_w / V_LINES;
        gc->StrokeLine(x, MARGIN_T, x, MARGIN_T + plot_h);
    }

    // Axis lines (brighter)
    gc->SetPen(wxPen(app_.GetTheme().GetAxisColor(), 1));
    gc->StrokeLine(MARGIN_L, MARGIN_T, MARGIN_L, MARGIN_T + plot_h);           // Y axis
    gc->StrokeLine(MARGIN_L, MARGIN_T + plot_h,
                   MARGIN_L + plot_w, MARGIN_T + plot_h);                       // X axis
}

void SpyPlot::DrawAxesLabels(wxDC& dc)
{
    wxSize sz = GetSize();

    // Y axis colour: selected trace colour in normalised mode, theme highlight otherwise
    const wxColour& y_axis_col = (!shared_axis_ && selected_trace_idx_ < traces_.size())
                                 ? traces_[selected_trace_idx_].colour
                                 : app_.GetTheme().GetHighlightColor();
    dc.SetTextForeground(y_axis_col);
    dc.SetFont(app_.GetTheme().GetBoldFont());

    double plot_h = sz.y - MARGIN_T - MARGIN_B;

    // Y labels — 6 ticks
    const int H_LINES = 5;
    for (int i = 0; i <= H_LINES; ++i) {
        double value = y_hi_ - i * (y_hi_ - y_lo_) / H_LINES;
        double py    = MARGIN_T + i * plot_h / H_LINES;

        std::ostringstream ss;
        ss << std::setprecision(4) << std::setw(8) << value;
        wxString label = wxString::FromUTF8(ss.str());

        wxSize text_sz = dc.GetTextExtent(label);
        dc.DrawText(label, MARGIN_L - text_sz.x - 4, (int)py - text_sz.y / 2);
    }

    // X labels — time offsets in seconds
    double plot_w  = sz.x - MARGIN_L - MARGIN_R;
    double t_now   = paused_ ? frozen_time_s_
                             : std::chrono::duration<double>(Clock::now() - start_).count();
    double t_right = t_now + pan_offset_s_;
    const int V_LINES = 5;

    // Choose precision so adjacent labels are always distinct
    double step = time_window_s_ / V_LINES;
    int x_prec = 1;
    if (step < 0.1)  x_prec = 2;
    if (step < 0.01) x_prec = 3;

    for (int i = 0; i <= V_LINES; ++i) {
        double t      = t_right - time_window_s_ + i * time_window_s_ / V_LINES;
        double px     = MARGIN_L + i * plot_w / V_LINES;

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(x_prec) << (t - t_right) << "s";
        wxString label = wxString::FromUTF8(ss.str());

        wxSize text_sz = dc.GetTextExtent(label);
        dc.DrawText(label, (int)px - text_sz.x / 2,
                    sz.y - MARGIN_B + 4);
    }

    // Key names stacked top-left, each in its trace colour.
    // Record bounding rects for right-click hit-testing.
    trace_label_rects_.clear();
    dc.SetFont(wxFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    int label_y = MARGIN_T + 2;
    for (size_t ti = 0; ti < traces_.size(); ++ti) {
        const auto& trace = traces_[ti];
        dc.SetTextForeground(trace.colour);
        wxString name = wxString::FromUTF8(trace.key);
        if (!shared_axis_ && ti == selected_trace_idx_)
            name = "> " + name;
        wxSize ext = dc.GetTextExtent(name);
        dc.DrawText(name, MARGIN_L + 4, label_y);
        trace_label_rects_.emplace_back(MARGIN_L + 4, label_y, ext.x, ext.y);
        label_y += ext.y + 2;
    }
}

void SpyPlot::DrawTrace(wxGraphicsContext* gc)
{
    double t_now  = paused_ ? frozen_time_s_
                            : std::chrono::duration<double>(Clock::now() - start_).count();
    double t_left = t_now + pan_offset_s_ - time_window_s_;

    for (size_t ti = 0; ti < traces_.size(); ++ti) {
        const auto& trace = traces_[ti];
        if (trace.data.size() < 2) continue;

        gc->SetPen(wxPen(trace.colour, 2));
        wxGraphicsPath path = gc->CreatePath();
        bool first = true;

        for (const auto& s : trace.data) {
            if (s.time_s < t_left) continue;
            double cx = ClientX(s.time_s);
            double cy = ClientY(ti, s.value);
            if (first) { path.MoveToPoint(cx, cy); first = false; }
            else        path.AddLineToPoint(cx, cy);
        }

        if (!first)
            gc->StrokePath(path);
    }
}

void SpyPlot::DrawLatestValue(wxGraphicsContext* gc)
{
    for (size_t ti = 0; ti < traces_.size(); ++ti) {
        const auto& trace = traces_[ti];
        if (trace.data.empty()) continue;
        double cy = ClientY(ti, trace.data.back().value);
        gc->SetPen(wxPen(trace.colour, 2));
        gc->StrokeLine(MARGIN_L - 6, cy, MARGIN_L, cy);
    }
}

// ── Panning (active only while paused) ───────────────────────────────────────

void SpyPlot::OnLeftDown(wxMouseEvent& e)
{
    wxPoint pos = e.GetPosition();

    // In normalised mode, left-click near a trace line selects it (sets axis scale).
    // Works whether live or paused. Checked before the paused guard.
    if (!shared_axis_ && !e.ShiftDown()) {
        static constexpr double TRACE_HIT_PX = 15.0;
        double t_now  = paused_ ? frozen_time_s_
                                : std::chrono::duration<double>(Clock::now() - start_).count();
        double t_left = t_now + pan_offset_s_ - time_window_s_;
        bool found = false;

        for (size_t ti = 0; ti < traces_.size() && !found; ++ti) {
            for (const auto& s : traces_[ti].data) {
                if (s.time_s < t_left) continue;
                double dx = pos.x - ClientX(s.time_s);
                double dy = pos.y - ClientY(ti, s.value);
                if (std::sqrt(dx*dx + dy*dy) <= TRACE_HIT_PX) {
                    selected_trace_idx_ = ti;
                    found = true;
                    break;
                }
            }
        }

        if (found) {
            Refresh();
            if (!paused_) return;   // live: only select trace, don't start pan
        }
    }

    if (!paused_) { e.Skip(); return; }

    // Any non-shift click cancels a pending link selection
    if (!e.ShiftDown() && shift_selected_marker_ >= 0) {
        shift_selected_marker_ = -1;
        Refresh();
    }

    // Shift+click: marker linking
    if (e.ShiftDown()) {
        for (int i = 0; i < static_cast<int>(markers_.size()); ++i) {
            double dx = pos.x - ClientX(markers_[i].time_s);
            double dy = pos.y - ClientY(markers_[i].trace_idx, markers_[i].value);
            if (std::sqrt(dx*dx + dy*dy) <= MARKER_HIT_PX) {
                if (shift_selected_marker_ < 0) {
                    // First endpoint selected — highlight it
                    shift_selected_marker_ = i;
                    Refresh();
                } else if (shift_selected_marker_ != i) {
                    // Second endpoint — create the link
                    CreateMarkerLink(shift_selected_marker_, i);
                    shift_selected_marker_ = -1;
                }
                return;
            }
        }
        // Shift+click on empty space — cancel pending selection
        shift_selected_marker_ = -1;
        Refresh();
        return;
    }

    // Marker drag takes priority — check circle hit first
    for (int i = 0; i < static_cast<int>(markers_.size()); ++i) {
        double dx = pos.x - ClientX(markers_[i].time_s);
        double dy = pos.y - ClientY(markers_[i].trace_idx, markers_[i].value);
        if (std::sqrt(dx*dx + dy*dy) <= MARKER_HIT_PX) {
            dragging_marker_ = i;
            CaptureMouse();
            return;
        }
    }

    // Fall through to pan
    dragging_      = true;
    last_drag_pos_ = pos;
    CaptureMouse();
}

void SpyPlot::OnLeftUp(wxMouseEvent& e)
{
    if (dragging_marker_ >= 0) {
        dragging_marker_ = -1;
        if (HasCapture()) ReleaseMouse();
        return;
    }
    if (!dragging_) { e.Skip(); return; }
    dragging_ = false;
    if (HasCapture()) ReleaseMouse();
}

void SpyPlot::OnMouseMotion(wxMouseEvent& e)
{
    if (!paused_) { e.Skip(); return; }

    wxPoint pos = e.GetPosition();

    // Marker drag
    if (dragging_marker_ >= 0) {
        Marker& m       = markers_[dragging_marker_];
        double new_time = PlotTime(pos.x);

        // Snap to nearest sample on the same trace by x distance
        const auto& trace = traces_[m.trace_idx];
        double best_dt = std::numeric_limits<double>::max();
        for (const auto& s : trace.data) {
            double dt = std::abs(s.time_s - new_time);
            if (dt < best_dt) {
                best_dt  = dt;
                m.time_s = s.time_s;
                m.value  = s.value;
            }
        }

        // Sync both labels to the new sample
        std::ostringstream yss;
        yss << std::setprecision(6) << m.value;
        m.y_ctrl->SetLabel(wxString::FromUTF8("y: " + yss.str()));

        std::ostringstream xss;
        xss << std::fixed << std::setprecision(3) << m.time_s << "s";
        m.x_ctrl->SetLabel(wxString::FromUTF8("x: " + xss.str()));

        RepositionMarkerControls();
        Refresh();
        return;
    }

    if (!dragging_) { e.Skip(); return; }
    auto_scale_ = false;

    wxPoint delta = pos - last_drag_pos_;
    last_drag_pos_ = pos;

    double plot_w = GetSize().x - MARGIN_L - MARGIN_R;
    double plot_h = GetSize().y - MARGIN_T - MARGIN_B;

    // Horizontal — pan time; dragging right moves view into the past
    double time_per_px = time_window_s_ / plot_w;
    pan_offset_s_ -= delta.x * time_per_px;
    pan_offset_s_  = std::min(0.0, pan_offset_s_);   // clamp: can't pan past now

    // Vertical — shift y range.
    // In normalised mode each trace has its own scale; shift all per-trace
    // ranges independently, then sync y_lo_/y_hi_ from the selected trace.
    if (!shared_axis_) {
        for (size_t ti = 0; ti < traces_.size(); ++ti) {
            if (ti >= per_trace_lo_.size()) continue;
            double range = per_trace_hi_[ti] - per_trace_lo_[ti];
            double vpp   = (range == 0.0) ? 1.0 : range / plot_h;
            double dy    = delta.y * vpp;
            per_trace_lo_[ti] += dy;
            per_trace_hi_[ti] += dy;
        }
        // Keep axis labels in sync with the selected trace
        size_t si = std::min(selected_trace_idx_,
                             traces_.empty() ? 0UL : traces_.size() - 1);
        if (si < per_trace_lo_.size()) {
            y_lo_ = per_trace_lo_[si];
            y_hi_ = per_trace_hi_[si];
        }
    } else {
        double value_per_px = (y_hi_ - y_lo_) / plot_h;
        double dy = delta.y * value_per_px;
        y_lo_ += dy;
        y_hi_ += dy;
    }

    Refresh();
}

// ── Context menu ──────────────────────────────────────────────────────────────

void SpyPlot::OnContextMenu(wxContextMenuEvent& e)
{
    // ── Hit-test trace key labels ─────────────────────────────────────────────
    wxPoint client_pos = ScreenToClient(e.GetPosition());
    for (size_t i = 0; i < trace_label_rects_.size() && i < traces_.size(); ++i) {
        if (!trace_label_rects_[i].Contains(client_pos)) continue;

        const int ID_REMOVE = wxID_HIGHEST + 301;
        wxMenu menu;
        menu.Append(ID_REMOVE, "Remove");
        menu.Bind(wxEVT_MENU, [this, i](wxCommandEvent&) {
            // Collect marker indices being removed (those belonging to trace i)
            std::unordered_set<size_t> dead_markers;
            for (size_t mi = 0; mi < markers_.size(); ++mi)
                if (markers_[mi].trace_idx == i) dead_markers.insert(mi);

            // Remove links that touch any dead marker
            for (auto& link : links_) {
                if (dead_markers.count(link.from_idx) || dead_markers.count(link.to_idx)) {
                    link.dy_ctrl->Destroy();
                    link.dx_ctrl->Destroy();
                }
            }
            links_.erase(
                std::remove_if(links_.begin(), links_.end(), [&](const MarkerLink& l) {
                    return dead_markers.count(l.from_idx) || dead_markers.count(l.to_idx);
                }),
                links_.end());

            // Destroy dead marker controls and erase them
            for (auto& m : markers_) {
                if (m.trace_idx == i) { m.x_ctrl->Destroy(); m.y_ctrl->Destroy(); }
            }
            markers_.erase(
                std::remove_if(markers_.begin(), markers_.end(),
                               [i](const Marker& m){ return m.trace_idx == i; }),
                markers_.end());

            // Fix trace indices for surviving markers
            for (auto& m : markers_)
                if (m.trace_idx > i) --m.trace_idx;

            // Fix marker indices in surviving links (removal shifts indices down)
            // Rebuild from_idx/to_idx by counting how many dead markers were below each
            for (auto& link : links_) {
                size_t shift_from = 0, shift_to = 0;
                for (size_t dead : dead_markers) {
                    if (dead < link.from_idx) ++shift_from;
                    if (dead < link.to_idx)   ++shift_to;
                }
                link.from_idx -= shift_from;
                link.to_idx   -= shift_to;
            }

            shift_selected_marker_ = -1;

            traces_.erase(traces_.begin() + i);
            if (i < per_trace_lo_.size()) per_trace_lo_.erase(per_trace_lo_.begin() + i);
            if (i < per_trace_hi_.size()) per_trace_hi_.erase(per_trace_hi_.begin() + i);
            if (!traces_.empty() && selected_trace_idx_ >= traces_.size())
                selected_trace_idx_ = traces_.size() - 1;
            trace_label_rects_.clear();   // stale — will repopulate on next paint

            // If back to a single trace, restore the leaf name as the pane caption
            if (traces_.size() == 1) {
                wxAuiManager& dock = app_.GetDockPanel()->GetDock();
                wxAuiPaneInfo* pane = &dock.GetPane(this);
                if (!pane->IsOk()) pane = &dock.GetPane(GetParent());
                if (pane->IsOk()) {
                    wxString caption = wxString::FromUTF8(traces_[0].key);
                    int dot = caption.Find('.', /*fromEnd=*/true);
                    if (dot != wxNOT_FOUND) caption = caption.Mid(dot + 1);
                    pane->Caption(caption);
                    dock.Update();
                }
            }

            Refresh();
        }, ID_REMOVE);
        PopupMenu(&menu);
        return;
    }

    // ── Regular plot context menu ─────────────────────────────────────────────
    enum {
        ID_PAUSE       = wxID_HIGHEST + 201,
        ID_AUTOSCALE   = wxID_HIGHEST + 202,
        ID_RESET       = wxID_HIGHEST + 203,
        ID_SHARED_AXIS = wxID_HIGHEST + 204,
    };

    wxMenu menu;
    menu.Append(ID_PAUSE,     paused_ ? "Resume" : "Pause");
    menu.Append(ID_AUTOSCALE, "Auto Scale");
    menu.Enable(ID_AUTOSCALE, !auto_scale_);
    menu.AppendSeparator();
    menu.AppendCheckItem(ID_SHARED_AXIS, "Shared Axis");
    menu.Check(ID_SHARED_AXIS, shared_axis_);
    menu.AppendSeparator();
    menu.Append(ID_RESET, "Reset");

    menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        paused_ = !paused_;
        if (paused_) {
            frozen_time_s_ = std::chrono::duration<double>(Clock::now() - start_).count();
        } else {
            pan_offset_s_ = 0.0;   // snap back to live view on resume
            ClearMarkers();
        }
    }, ID_PAUSE);

    menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        auto_scale_ = true;
    }, ID_AUTOSCALE);

    menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        shared_axis_ = !shared_axis_;
        if (!shared_axis_) {
            // Seed per-trace ranges from current global range as a sensible start
            per_trace_lo_.assign(traces_.size(), y_lo_);
            per_trace_hi_.assign(traces_.size(), y_hi_);
            selected_trace_idx_ = std::min(selected_trace_idx_,
                                           traces_.empty() ? 0UL : traces_.size() - 1);
        }
        Refresh();
    }, ID_SHARED_AXIS);

    menu.Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        if (paused_) {
            // While paused: clear annotations only
            ClearMarkers();
        } else {
            // While live: full state reset
            dragging_      = false;
            pan_offset_s_  = 0.0;
            time_window_s_ = 10.0;
            auto_scale_    = true;
            if (HasCapture()) ReleaseMouse();
            ClearMarkers();
        }
        Refresh();
    }, ID_RESET);

    PopupMenu(&menu);
}

void SpyPlot::OnMouseWheel(wxMouseEvent& e)
{
    // Positive rotation = scroll up = zoom in
    const double factor = (e.GetWheelRotation() > 0) ? (1.0 / 1.15) : 1.15;

    if (e.GetModifiers() & wxMOD_SHIFT) {
        // Shift+scroll — scale Y axis around midpoint (shared axis only; no-op in normalised mode)
        if (shared_axis_) {
            auto_scale_ = false;
            double mid  = (y_hi_ + y_lo_) / 2.0;
            double half = (y_hi_ - y_lo_) / 2.0 * factor;
            y_lo_ = mid - half;
            y_hi_ = mid + half;
        }
    } else {
        // Scroll — scale X time window symmetrically around centre
        time_window_s_ = std::max(0.1, time_window_s_ * factor);
    }

    Refresh();
}

// ── Paint ─────────────────────────────────────────────────────────────────────
void SpyPlot::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.Clear();

    UpdateYRange();

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
    if (!gc) return;

    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    DrawBackground(gc);
    DrawGrid(gc);
    DrawTrace(gc);
    DrawMarkerLinks(gc);
    DrawMarkers(gc);
    DrawLatestValue(gc);

    delete gc;

    // Axis labels drawn with plain DC (better text rendering than GC for small fonts)
    DrawAxesLabels(dc);

    // Keep marker controls pinned to their data points
    RepositionMarkerControls();
}

// ── Layout persistence ────────────────────────────────────────────────────────

void SpyPlot::SerializeTo(boost::property_tree::ptree& node) const
{
    namespace pt = boost::property_tree;

    // Trace keys — ordered list
    pt::ptree keys_node;
    for (const auto& t : traces_) {
        pt::ptree item;
        item.put("", t.key);
        keys_node.push_back({"", item});
    }
    node.add_child("keys", keys_node);

    node.put("shared_axis",    shared_axis_);
    node.put("auto_scale",     auto_scale_);
    node.put("time_window_s",  time_window_s_);
    node.put("selected_trace", selected_trace_idx_);
    node.put("y_lo",           y_lo_);
    node.put("y_hi",           y_hi_);

    // Per-trace Y ranges (meaningful in non-shared axis mode)
    pt::ptree ranges_node;
    for (size_t i = 0; i < per_trace_lo_.size(); ++i) {
        pt::ptree r;
        r.put("lo", per_trace_lo_[i]);
        r.put("hi", per_trace_hi_[i]);
        ranges_node.push_back({"", r});
    }
    node.add_child("per_trace_ranges", ranges_node);
}

void SpyPlot::DeserializeFrom(const boost::property_tree::ptree& node)
{
    namespace pt = boost::property_tree;

    // Rebuild traces from saved key list
    pt::ptree empty;
    std::vector<std::string> keys;
    for (const auto& item : node.get_child("keys", empty))
        keys.push_back(item.second.get_value<std::string>());

    if (!keys.empty()) {
        SetKey(keys[0]);
        for (size_t i = 1; i < keys.size(); ++i)
            AddTrace(keys[i]);
    }

    shared_axis_        = node.get<bool>  ("shared_axis",    true);
    auto_scale_         = node.get<bool>  ("auto_scale",     true);
    time_window_s_      = node.get<double>("time_window_s",  10.0);
    selected_trace_idx_ = node.get<size_t>("selected_trace", 0);
    y_lo_               = node.get<double>("y_lo",           0.0);
    y_hi_               = node.get<double>("y_hi",           1.0);

    // Per-trace Y ranges
    per_trace_lo_.clear();
    per_trace_hi_.clear();
    for (const auto& item : node.get_child("per_trace_ranges", empty)) {
        per_trace_lo_.push_back(item.second.get<double>("lo", -1.0));
        per_trace_hi_.push_back(item.second.get<double>("hi",  1.0));
    }

    // Ensure vectors stay in sync with traces_ (guard against truncated JSON)
    per_trace_lo_.resize(traces_.size(), -1.0);
    per_trace_hi_.resize(traces_.size(),  1.0);

    // Clamp selected_trace_idx_ into range
    if (!traces_.empty() && selected_trace_idx_ >= traces_.size())
        selected_trace_idx_ = traces_.size() - 1;
}

} // namespace spycat