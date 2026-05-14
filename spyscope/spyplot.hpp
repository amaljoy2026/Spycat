// spyplot.hpp
#ifndef __SPYSCOPE_PLOT_HPP__
#define __SPYSCOPE_PLOT_HPP__

#include <wx/wx.h>
#include <wx/graphics.h>
#include <chrono>
#include <string>
#include <boost/circular_buffer.hpp>
#include <boost/property_tree/ptree.hpp>

#include "datasource.hpp"
#include "app.hpp"

// Forward-declare SpyScope (global namespace) — full definition in app.hpp,
// included only in spyplot.cpp to avoid a circular header dependency.
class SpyScope;

namespace spycat
{

// Forward-declare DataSource — full definition included in spyplot.cpp
class DataSource;

struct Sample {
    double time_s;   // seconds since plot creation
    double value;
};

class SpyPlot : public wxPanel, public DataObserver
{
public:
    SpyPlot(wxWindow* parent,
            App& app,
            const std::string& key    = "signal",
            wxWindowID id             = wxID_ANY,
            const wxPoint& pos        = wxDefaultPosition,
            const wxSize& size        = wxDefaultSize,
            long style                = wxTAB_TRAVERSAL | wxCLIP_CHILDREN);
    ~SpyPlot() override;

    // Replace all traces with a single key
    void SetKey(const std::string& key);

    // Add a new trace; no-op if the key is already present
    void AddTrace(const std::string& key);

    // Push a value to the first trace (external use)
    void PushSample(double value);
    void OnDataPoll() override;

    // Configuration
    void SetTimeWindow(double seconds) { time_window_s_ = seconds; }
    void SetAutoScale(bool enabled)    { auto_scale_    = enabled;  }
    void SetYRange(double lo, double hi) { y_lo_ = lo; y_hi_ = hi; auto_scale_ = false; }

    // Layout persistence
    void SerializeTo(boost::property_tree::ptree& node) const;
    void DeserializeFrom(const boost::property_tree::ptree& node);

private:
    // Coordinate transforms
    double ClientX(double time_s)                    const;   // plot time  → pixel x
    double ClientY(double value)                     const;   // plot value → pixel y (shared axis)
    double ClientY(size_t trace_idx, double value)   const;   // plot value → pixel y (per-trace)
    double PlotTime(int px)                          const;   // pixel x    → plot time
    double PlotValue(int py)                         const;   // pixel y    → plot value

    // Draw helpers
    void DrawBackground(wxGraphicsContext* gc);
    void DrawGrid(wxGraphicsContext* gc);
    void DrawAxesLabels(wxDC& dc);
    void DrawTrace(wxGraphicsContext* gc);
    void DrawLatestValue(wxGraphicsContext* gc);

    // Auto-scale: compute y range from visible samples
    void UpdateYRange();

    // Events
    void OnPaint(wxPaintEvent&);
    void OnSize(wxSizeEvent& e) { Refresh(); e.Skip(); }
    void OnPaintTimer(wxTimerEvent&) { if (!paused_) Refresh(); }
    void OnContextMenu(wxContextMenuEvent&);
    void OnMouseWheel(wxMouseEvent&);
    void OnLeftDown(wxMouseEvent&);
    void OnLeftUp(wxMouseEvent&);
    void OnLeftDClick(wxMouseEvent&);
    void OnMouseMotion(wxMouseEvent&);
    void OnKeyDown(wxKeyEvent&);

    // Draw helpers
    void DrawMarkers(wxGraphicsContext* gc);
    void DrawMarkerLinks(wxGraphicsContext* gc);
    void RepositionMarkerControls();

    // Marker linking
    void CreateMarkerLink(int from_idx, int to_idx);

    // Helper: clear and destroy all markers and links (called on resume / reset)
    void ClearMarkers();

    // Per-trace state
    struct Trace {
        std::string                    key;
        boost::circular_buffer<Sample> data { 36000 };
        wxColour                       colour;
        Trace(const std::string& k, const wxColour& c) : key(k), colour(c) {}
    };

    std::vector<Trace> traces_;

    // ── Markers ───────────────────────────────────────────────────────────────
    struct Marker {
        size_t        trace_idx;
        double        time_s;
        double        value;
        wxStaticText* x_ctrl;   // time display (top)
        wxStaticText* y_ctrl;   // value display (below x_ctrl)
    };

    std::vector<Marker> markers_;
    int dragging_marker_       = -1;   // index of marker being dragged, -1 if none
    int shift_selected_marker_ = -1;   // first shift-clicked marker awaiting link partner

    // ── Marker links ──────────────────────────────────────────────────────────
    struct MarkerLink {
        size_t        from_idx;
        size_t        to_idx;
        wxStaticText* dy_ctrl;   // Δy label (top)
        wxStaticText* dx_ctrl;   // Δx label (below dy)
    };

    std::vector<MarkerLink> links_;
    App& app_;

    // Time
    using Clock = std::chrono::steady_clock;
    Clock::time_point start_ = Clock::now();

    // View config
    double time_window_s_ = 10.0;   // how many seconds visible on x axis
    bool   auto_scale_    = true;
    double y_lo_          = 0.0;
    double y_hi_          = 1.0;

    // Margins (pixels)
    static constexpr int MARGIN_L = 65;   // room for y labels
    static constexpr int MARGIN_R = 20;
    static constexpr int MARGIN_T = 20;
    static constexpr int MARGIN_B = 35;   // room for x labels

    // Colours
    static const wxColour COL_BG;
    static const wxColour COL_GRID;
    static const wxColour COL_AXIS;
    static const wxColour COL_TRACE;
    static const wxColour COL_TEXT;
    static const wxColour COL_VALUE;

    // Bounding rects of per-trace key labels painted in DrawAxesLabels.
    // Populated each paint; used by OnContextMenu for hit-testing.
    std::vector<wxRect> trace_label_rects_;

    // Shared axis / normalised mode
    bool                shared_axis_        = true;
    size_t              selected_trace_idx_ = 0;
    std::vector<double> per_trace_lo_;
    std::vector<double> per_trace_hi_;

    bool     paused_         = false;
    bool     dragging_       = false;
    wxPoint  last_drag_pos_;
    double   pan_offset_s_   = 0.0;   // ≤ 0, horizontal pan while paused
    double   frozen_time_s_  = 0.0;   // t_now captured at pause

    // Monotonically increasing — never decremented when a trace is removed.
    // Ensures that re-adding a trace after removal always gets a fresh colour.
    size_t   color_counter_  = 0;

    wxTimer paint_timer_;
};

} // namespace spycat

#endif // __SPYSCOPE_PLOT_HPP__