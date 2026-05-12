// spyplot.cpp
#include "spyplot.hpp"
#include <wx/dcbuffer.h>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <variant>

namespace spycat
{

// ── Colour palette ────────────────────────────────────────────────────────────
const wxColour SpyPlot::COL_BG    { 0xFF, 0xFF, 0xFF };
const wxColour SpyPlot::COL_GRID  { 0x00, 0x33, 0x00 };
const wxColour SpyPlot::COL_AXIS  { 0x00, 0x66, 0x00 };
const wxColour SpyPlot::COL_TRACE { 0x00, 0x66, 0x00 };
const wxColour SpyPlot::COL_TEXT  { 0x00, 0x66, 0x00 };
const wxColour SpyPlot::COL_VALUE { 0xFF, 0xFF, 0xFF };

// ── Construction ──────────────────────────────────────────────────────────────
SpyPlot::SpyPlot(wxWindow* parent, DataSource* source, const std::string& key,
                 wxWindowID id, const wxPoint& pos,
                 const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
    , key_(key)
    , source_(source)
    , paint_timer_(this)
    , data_timer_(this)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);   // required for wxAutoBufferedPaintDC
    SetBackgroundColour(COL_BG);
    SetMinSize({ 200, 100 });

    Bind(wxEVT_PAINT, &SpyPlot::OnPaint,      this);
    Bind(wxEVT_SIZE,  &SpyPlot::OnSize,        this);
    Bind(wxEVT_TIMER, &SpyPlot::OnPaintTimer,  this, paint_timer_.GetId());
    Bind(wxEVT_TIMER, &SpyPlot::OnDataTimer,   this, data_timer_.GetId());

    paint_timer_.Start(16);   // ~60 Hz repaint
    data_timer_.Start(16);    // ~60 Hz data ingestion
}

// ── Public API ────────────────────────────────────────────────────────────────
void SpyPlot::SetKey(const std::string& key)
{
    key_ = key;
    data_.clear();
}

void SpyPlot::PushSample(double value)
{
    double t = std::chrono::duration<double>(Clock::now() - start_).count();
    data_.push_back({ t, value });
}

// ── Data timer ────────────────────────────────────────────────────────────────
void SpyPlot::OnDataTimer(wxTimerEvent&)
{
    if (!source_) return;

    source_->Poll();

    auto entry = source_->Get(key_);
    if (!entry) return;

    double value = std::visit([](auto&& v) -> double {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_arithmetic_v<T>)
            return static_cast<double>(v);
        return 0.0;   // string / raw blob — not plottable
    }, entry->value);

    PushSample(value);
}

// ── Coordinate transforms ─────────────────────────────────────────────────────
double SpyPlot::ClientX(double time_s) const
{
    // Most-recent time sits at the right edge
    double t_now   = std::chrono::duration<double>(Clock::now() - start_).count();
    double t_left  = t_now - time_window_s_;
    double plot_w  = GetSize().x - MARGIN_L - MARGIN_R;
    return MARGIN_L + (time_s - t_left) / time_window_s_ * plot_w;
}

double SpyPlot::ClientY(double value) const
{
    double plot_h  = GetSize().y - MARGIN_T - MARGIN_B;
    double range   = (y_hi_ == y_lo_) ? 1.0 : (y_hi_ - y_lo_);
    // y=0 at bottom: flip so higher value = higher on screen
    return MARGIN_T + (1.0 - (value - y_lo_) / range) * plot_h;
}

double SpyPlot::PlotTime(int px) const
{
    double t_now  = std::chrono::duration<double>(Clock::now() - start_).count();
    double t_left = t_now - time_window_s_;
    double plot_w = GetSize().x - MARGIN_L - MARGIN_R;
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
    if (!auto_scale_ || data_.empty()) return;

    double t_now  = std::chrono::duration<double>(Clock::now() - start_).count();
    double t_left = t_now - time_window_s_;

    double lo =  std::numeric_limits<double>::max();
    double hi = -std::numeric_limits<double>::max();

    for (const auto& s : data_) {
        if (s.time_s < t_left) continue;
        lo = std::min(lo, s.value);
        hi = std::max(hi, s.value);
    }

    if (lo == hi) { lo -= 1.0; hi += 1.0; }   // flat signal: show ±1 band

    // 5% margin top and bottom
    double margin = (hi - lo) * 0.05;
    y_lo_ = lo - margin;
    y_hi_ = hi + margin;
}

// ── Drawing ───────────────────────────────────────────────────────────────────
void SpyPlot::DrawBackground(wxGraphicsContext* gc)
{
    wxSize sz = GetSize();
    gc->SetBrush(wxBrush(COL_BG));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawRectangle(0, 0, sz.x, sz.y);
}

void SpyPlot::DrawGrid(wxGraphicsContext* gc)
{
    wxSize sz    = GetSize();
    double plot_w = sz.x - MARGIN_L - MARGIN_R;
    double plot_h = sz.y - MARGIN_T - MARGIN_B;

    // 5 horizontal grid lines
    gc->SetPen(wxPen(COL_GRID, 1, wxPENSTYLE_DOT));
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
    gc->SetPen(wxPen(COL_AXIS, 1));
    gc->StrokeLine(MARGIN_L, MARGIN_T, MARGIN_L, MARGIN_T + plot_h);           // Y axis
    gc->StrokeLine(MARGIN_L, MARGIN_T + plot_h,
                   MARGIN_L + plot_w, MARGIN_T + plot_h);                       // X axis
}

void SpyPlot::DrawAxesLabels(wxDC& dc)
{
    wxSize sz = GetSize();
    dc.SetTextForeground(COL_TEXT);
    dc.SetFont(wxFont(12, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));

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
    double t_now   = std::chrono::duration<double>(Clock::now() - start_).count();
    const int V_LINES = 5;
    for (int i = 0; i <= V_LINES; ++i) {
        double t      = t_now - time_window_s_ + i * time_window_s_ / V_LINES;
        double px     = MARGIN_L + i * plot_w / V_LINES;

        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << (t - t_now) << "s";
        wxString label = wxString::FromUTF8(ss.str());

        wxSize text_sz = dc.GetTextExtent(label);
        dc.DrawText(label, (int)px - text_sz.x / 2,
                    sz.y - MARGIN_B + 4);
    }

    // Key name top-left
    dc.SetTextForeground(COL_TRACE);
    dc.SetFont(wxFont(12, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    dc.DrawText(wxString::FromUTF8(key_), MARGIN_L + 4, MARGIN_T + 2);
}

void SpyPlot::DrawTrace(wxGraphicsContext* gc)
{
    if (data_.size() < 2) return;

    double t_now  = std::chrono::duration<double>(Clock::now() - start_).count();
    double t_left = t_now - time_window_s_;

    gc->SetPen(wxPen(COL_TRACE, 2));

    wxGraphicsPath path = gc->CreatePath();
    bool first = true;

    for (const auto& s : data_) {
        if (s.time_s < t_left) continue;

        double cx = ClientX(s.time_s);
        double cy = ClientY(s.value);

        if (first) { path.MoveToPoint(cx, cy); first = false; }
        else        path.AddLineToPoint(cx, cy);
    }

    if (!first)
        gc->StrokePath(path);
}

void SpyPlot::DrawLatestValue(wxGraphicsContext* gc)
{
    if (data_.empty()) return;

    double latest = data_.back().value;

    std::ostringstream ss;
    ss << std::setprecision(6) << latest;
    wxString label = wxString::FromUTF8(ss.str());

    // Draw current value at right edge of trace
    double cy = ClientY(latest);
    double cx = GetSize().x - MARGIN_R - 4;

    gc->SetPen(wxPen(COL_VALUE, 1));
    gc->SetBrush(*wxTRANSPARENT_BRUSH);

    // Horizontal tick at current value on Y axis
    gc->StrokeLine(MARGIN_L - 4, cy, MARGIN_L, cy);
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
    DrawLatestValue(gc);

    delete gc;

    // Axis labels drawn with plain DC (better text rendering than GC for small fonts)
    DrawAxesLabels(dc);
}

} // namespace spycat