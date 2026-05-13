// spyplot.hpp
#ifndef __SPYSCOPE_PLOT_HPP__
#define __SPYSCOPE_PLOT_HPP__

#include <wx/wx.h>
#include <wx/graphics.h>
#include <chrono>
#include <string>
#include <boost/circular_buffer.hpp>

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

class SpyPlot : public wxPanel
{
public:
    SpyPlot(wxWindow* parent,
            SpyScope&          app,
            const std::string& key    = "signal",
            wxWindowID id             = wxID_ANY,
            const wxPoint& pos        = wxDefaultPosition,
            const wxSize& size        = wxDefaultSize,
            long style                = wxTAB_TRAVERSAL | wxCLIP_CHILDREN);

    // Change which key is plotted at runtime
    void SetKey(const std::string& key);

    // Push a new value directly (still usable externally)
    void PushSample(double value);

    // Configuration
    void SetTimeWindow(double seconds) { time_window_s_ = seconds; }
    void SetAutoScale(bool enabled)    { auto_scale_    = enabled;  }
    void SetYRange(double lo, double hi) { y_lo_ = lo; y_hi_ = hi; auto_scale_ = false; }

private:
    // Coordinate transforms
    double ClientX(double time_s)  const;   // plot time  → pixel x
    double ClientY(double value)   const;   // plot value → pixel y
    double PlotTime(int px)        const;   // pixel x    → plot time
    double PlotValue(int py)       const;   // pixel y    → plot value

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
    void OnPaintTimer(wxTimerEvent&) { Refresh(); }
    void OnDataTimer(wxTimerEvent&);

    // Identity
    std::string key_;
    DataSource* source_;

    // Ring buffer — 10 min at 60 Hz = 36000 samples, cap at that
    boost::circular_buffer<Sample> data_ { 36000 };

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

    wxTimer paint_timer_;
    wxTimer data_timer_;
};

} // namespace spycat

#endif // __SPYSCOPE_PLOT_HPP__