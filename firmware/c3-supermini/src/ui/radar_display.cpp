#include "ui/radar_display.h"

#include <esp_heap_caps.h>
#include <lgfx/v1/lgfx_fonts.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/adsb_client.h"
#include "services/display_settings.h"
#include "services/radar_location.h"
#include "services/weather_time.h"
#include "ui/label_layout.h"
#include "ui/radar_projection.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"
#include "ui/runway_overlay.h"

namespace lgfx_fonts = lgfx::v1::fonts;

namespace ui {
namespace radar {

uint16_t kColorBackground = 0x0000;
uint16_t kColorGrid = 0x0320;
uint16_t kColorLabel = 0xFFFF;
uint16_t kColorCenter = 0xFFFF;
uint16_t kColorAircraft = 0x001F;
uint16_t kColorLightAircraft = 0xFFE0;
uint16_t kColorMilitaryAircraft = 0x07E0;
uint16_t kColorTagAirline = 0xBDF7;
uint16_t kColorTrackVector = 0xFFFF;
uint16_t kColorTagType = 0x5DFF;
uint16_t kColorTagAltitude = 0xFFE0;
uint16_t kColorRunway = 0x4D5F;
uint16_t kColorRunwayLabel = 0x7DFF;
uint16_t kColorFooterBackground = 0x0084;

}  // namespace radar

namespace {

bool s_label_metrics_ready = false;
bool s_cardinal_use_vlw = false;
bool s_scale_use_vlw = false;
float s_cardinal_vlw_size = 0.56f;
float s_scale_vlw_size = 0.50f;
float s_tag_vlw_size = 0.56f;
const lgfx::GFXfont* s_cardinal_gfx = &lgfx_fonts::FreeSansBold12pt7b;
const lgfx::GFXfont* s_scale_gfx = &lgfx_fonts::FreeSansBold9pt7b;
const lgfx::GFXfont* s_tag_gfx = &lgfx_fonts::FreeSansBold12pt7b;

bool s_tag_label_metrics_ready = false;
bool s_tag_use_vlw = false;
bool s_footer_metrics_ready = false;
bool s_footer_use_vlw = false;
float s_footer_vlw_size = 0.36f;


lgfx::LovyanGFX* s_draw = &tft;
LGFX_Sprite s_frame(&tft);
bool s_frame_ready = false;

class DrawScope {
 public:
  explicit DrawScope(lgfx::LovyanGFX& gfx) : prev_(s_draw) { s_draw = &gfx; }
  ~DrawScope() { s_draw = prev_; }

 private:
  lgfx::LovyanGFX* prev_;
};

float configuredTextScale() {
  return static_cast<float>(services::settings::textScalePercent()) / 100.0f;
}

void applyBitmapTextScale(lgfx::LGFXBase& gfx) {
  gfx.setTextSize(configuredTextScale());
}

int absDiff(int a, int b) { return std::abs(a - b); }

int measureGfxHeight(const lgfx::GFXfont& font) {
  tft.setFont(&font);
  tft.setTextSize(1);
  return tft.fontHeight();
}

int measureVlwHeight(float size) {
  tft.setTextSize(size);
  return tft.fontHeight();
}

float findVlwSizeForHeight(int target_px) {
  float lo = 0.25f;
  float hi = 1.2f;
  for (int i = 0; i < 16; ++i) {
    const float mid = (lo + hi) * 0.5f;
    if (measureVlwHeight(mid) < target_px) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return hi;
}

void applyScaleStyle();

const lgfx::GFXfont* pickGfxFontClosest(
    int target_px, const lgfx::GFXfont* const* candidates, size_t count) {
  const lgfx::GFXfont* best = candidates[0];
  int best_diff = absDiff(measureGfxHeight(*best), target_px);

  for (size_t i = 1; i < count; ++i) {
    const int diff = absDiff(measureGfxHeight(*candidates[i]), target_px);
    if (diff < best_diff) {
      best_diff = diff;
      best = candidates[i];
    }
  }
  return best;
}

void initLabelMetrics() {
  if (s_label_metrics_ready) {
    return;
  }

  const int cardinal_target = radar::kCardinalLabelHeightPx;

  if (displayFontIsSmooth()) {
    s_cardinal_use_vlw = true;
    s_cardinal_vlw_size = findVlwSizeForHeight(cardinal_target);
    const int cardinal_h = measureVlwHeight(s_cardinal_vlw_size);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    s_scale_use_vlw = true;
    s_scale_vlw_size = findVlwSizeForHeight(scale_target);
  } else {
    const lgfx::GFXfont* cardinal_candidates[] = {
        &lgfx_fonts::FreeSansBold12pt7b, &lgfx_fonts::FreeSansBold9pt7b};
    s_cardinal_gfx =
        pickGfxFontClosest(cardinal_target, cardinal_candidates, 2);
    s_cardinal_use_vlw = false;

    const int cardinal_h = measureGfxHeight(*s_cardinal_gfx);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    const lgfx::GFXfont* scale_candidates[] = {
        &lgfx_fonts::FreeSansBold9pt7b, &lgfx_fonts::FreeSansBold12pt7b};
    s_scale_gfx = pickGfxFontClosest(scale_target, scale_candidates, 2);
    s_scale_use_vlw = false;
  }

  s_label_metrics_ready = true;
}

void initTagLabelMetrics() {
  if (s_tag_label_metrics_ready) {
    return;
  }

  const int target = radar::kAircraftTagLabelHeightPx;
  if (displayFontIsSmooth()) {
    s_tag_use_vlw = true;
    s_tag_vlw_size = findVlwSizeForHeight(target);
  } else {
    const lgfx::GFXfont* tag_candidates[] = {
        &lgfx_fonts::FreeSansBold12pt7b, &lgfx_fonts::FreeSansBold9pt7b};
    s_tag_gfx = pickGfxFontClosest(target, tag_candidates, 2);
    s_tag_use_vlw = false;
  }

  s_tag_label_metrics_ready = true;
}

void initFooterMetrics() {
  if (s_footer_metrics_ready) {
    return;
  }
  if (displayFontIsSmooth()) {
    s_footer_use_vlw = true;
    s_footer_vlw_size =
        findVlwSizeForHeight(radar::kFooterLabelHeightPx);
  }
  s_footer_metrics_ready = true;
}

void initPalette() {
  radar::kColorBackground = tft.color565(radar::kBgR, radar::kBgG, radar::kBgB);
  radar::kColorGrid = tft.color565(radar::kGridR, radar::kGridG, radar::kGridB);
  radar::kColorLabel = tft.color565(255, 255, 255);
  radar::kColorCenter = tft.color565(255, 255, 255);
  // GC9A01 BGR panel: swap R/B in color565 so logical red renders red on
  // screen. The other aircraft colours go through the same mapping.
  const auto aircraft_color = [](uint8_t r, uint8_t g, uint8_t b) {
    return config::kDisplayRgbOrder ? tft.color565(b, g, r)
                                    : tft.color565(r, g, b);
  };
  radar::kColorAircraft = aircraft_color(radar::kAircraftR, radar::kAircraftG,
                                         radar::kAircraftB);
  radar::kColorLightAircraft =
      aircraft_color(radar::kLightAircraftR, radar::kLightAircraftG,
                     radar::kLightAircraftB);
  radar::kColorMilitaryAircraft =
      aircraft_color(radar::kMilitaryAircraftR, radar::kMilitaryAircraftG,
                     radar::kMilitaryAircraftB);
  radar::kColorTagAirline =
      tft.color565(radar::kTagAirlineR, radar::kTagAirlineG, radar::kTagAirlineB);
  radar::kColorTrackVector =
      tft.color565(radar::kTrackR, radar::kTrackG, radar::kTrackB);
  radar::kColorTagType =
      tft.color565(radar::kTagTypeR, radar::kTagTypeG, radar::kTagTypeB);
  radar::kColorTagAltitude =
      tft.color565(radar::kTagAltR, radar::kTagAltG, radar::kTagAltB);
  radar::kColorRunway =
      tft.color565(radar::kRunwayR, radar::kRunwayG, radar::kRunwayB);
  radar::kColorRunwayLabel = tft.color565(radar::kRunwayLabelR, radar::kRunwayLabelG,
                                          radar::kRunwayLabelB);
  radar::kColorFooterBackground =
      tft.color565(radar::kFooterBgR, radar::kFooterBgG, radar::kFooterBgB);
}

// Shared with the runway overlay; see ui/radar_projection.h.
using radar::candidateRect;
using radar::findFreeLabelSpot;
using radar::LabelOccupancy;
using radar::LabelRect;
using radar::latLonToScreen;
using radar::offsetKmFromCenter;

float innerRingMaxKm() {
  const float outer_km = radar::rangeCurrent().outer_km;
  return outer_km * (static_cast<float>(radar::kGridOuterRadius -
                                       radar::kAircraftInsideRingInsetPx) /
                     static_cast<float>(radar::kGridOuterRadius));
}

bool isInsideOuterRingKm(float dist_km) { return dist_km <= innerRingMaxKm(); }

int distSqFromCenter(int x, int y) {
  const int dx = x - radar::kCenterX;
  const int dy = y - radar::kCenterY;
  return dx * dx + dy * dy;
}

/** Rim dot from true bearing; always on screen edge (even if target is 50+ km away). */
bool beyondRingEdgeDotFromLatLon(float lat, float lon, int* out_x, int* out_y) {
  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);
  if (dist_km < 0.01f) {
    return false;
  }
  if (isInsideOuterRingKm(dist_km)) {
    return false;
  }

  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int rim_r = radar::kCenterX - radar::kBeyondRingScreenMarginPx;
  float right_km = 0.0f;
  float up_km = 0.0f;
  radar::rotateToScreen(dx_km, dy_km, &right_km, &up_km);
  const float angle_rad = atan2f(right_km, up_km);

  *out_x = cx + static_cast<int>(lroundf(sinf(angle_rad) * rim_r));
  *out_y = cy - static_cast<int>(lroundf(cosf(angle_rad) * rim_r));
  return true;
}

void drawBeyondRingDot(int x, int y, uint16_t color) {
  s_draw->fillSmoothCircle(x, y, radar::kBeyondRingDotRadiusPx, color);
}

void clipPointToOuterRing(int x0, int y0, int* x1, int* y1) {
  const int max_r = radar::kGridOuterRadius;
  const int max_r_sq = max_r * max_r;
  if (distSqFromCenter(*x1, *y1) <= max_r_sq) {
    return;
  }

  const int dx = *x1 - x0;
  const int dy = *y1 - y0;
  float t = 1.0f;
  for (int step = 0; step < 20; ++step) {
    const int px = x0 + static_cast<int>(lroundf(dx * t));
    const int py = y0 + static_cast<int>(lroundf(dy * t));
    if (distSqFromCenter(px, py) <= max_r_sq) {
      *x1 = px;
      *y1 = py;
      return;
    }
    t -= 0.05f;
    if (t <= 0.0f) {
      *x1 = x0;
      *y1 = y0;
      return;
    }
  }
}

int speedLineLengthPx(float gs_knots) {
  if (gs_knots <= 0.0f) {
    return 0;
  }

  // Fixed screen scale: 60 s horizon at gs, not tied to current range zoom.
  constexpr float kKmPerKnotPerHorizon =
      1.852f * radar::kAircraftTrackHorizonSec / 3600.0f;
  const float px =
      gs_knots * kKmPerKnotPerHorizon * radar::kGridOuterRadius /
      radar::kAircraftTrackRefOuterKm * radar::kAircraftTrackLengthScale;

  const int len = static_cast<int>(px + 0.5f);
  if (len < radar::kAircraftSpeedLineMinPx) {
    return radar::kAircraftSpeedLineMinPx;
  }
  return len;
}

/** heading_deg is a screen angle (clockwise from up); scale 1 = full size. */
void noseTip(int cx, int cy, float heading_deg, int* tip_x, int* tip_y,
             float scale = 1.0f) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = heading_deg * kDegToRad;
  const float len = radar::kAircraftNoseLenPx * scale;
  *tip_x = cx + static_cast<int>(lroundf(sinf(rad) * len));
  *tip_y = cy - static_cast<int>(lroundf(cosf(rad) * len));
}

void drawHeadingTriangle(int cx, int cy, float heading_deg, uint16_t color,
                         float scale = 1.0f) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = heading_deg * kDegToRad;
  const float sin_h = sinf(rad);
  const float cos_h = cosf(rad);

  int tip_x = 0;
  int tip_y = 0;
  noseTip(cx, cy, heading_deg, &tip_x, &tip_y, scale);

  const float tail_len = radar::kAircraftTailLenPx * scale;
  const float tail_half = radar::kAircraftTailHalfPx * scale;
  const int base_x = cx - static_cast<int>(lroundf(sin_h * tail_len));
  const int base_y = cy + static_cast<int>(lroundf(cos_h * tail_len));

  const int wing_x = static_cast<int>(lroundf(cos_h * tail_half));
  const int wing_y = static_cast<int>(lroundf(sin_h * tail_half));

  s_draw->fillTriangle(tip_x, tip_y, base_x + wing_x, base_y + wing_y,
                       base_x - wing_x, base_y - wing_y, color);
}

void drawSpeedVector(int cx, int cy, float heading_deg, float track_deg,
                     float gs_knots, uint16_t color, float scale = 1.0f) {
  const int len = speedLineLengthPx(gs_knots);
  if (len <= 0) {
    return;
  }

  int tip_x = 0;
  int tip_y = 0;
  noseTip(cx, cy, heading_deg, &tip_x, &tip_y, scale);

  constexpr float kDegToRad = 0.01745329252f;
  const float rad = track_deg * kDegToRad;
  int ex = tip_x + static_cast<int>(lroundf(sinf(rad) * len));
  int ey = tip_y - static_cast<int>(lroundf(cosf(rad) * len));
  clipPointToOuterRing(tip_x, tip_y, &ex, &ey);
  if (ex == tip_x && ey == tip_y) {
    return;
  }
  s_draw->drawWideLine(tip_x, tip_y, ex, ey, radar::kAircraftTrackLineHalfWidth,
                       color);
}

void applyTagStyle() {
  if (s_tag_use_vlw) {
    displayFontSetSmoothSize(*s_draw,
                             s_tag_vlw_size * configuredTextScale());
  } else {
    displayFontSetBitmap(*s_draw, s_tag_gfx);
    applyBitmapTextScale(*s_draw);
  }
}

bool showAirlineLine(const services::adsb::Aircraft& plane) {
  return plane.airline[0] != '\0' && services::settings::showAirlineNames();
}

int measureTagBlockWidth(const services::adsb::Aircraft& plane) {
  applyTagStyle();
  int max_w = 0;
  const char* identity =
      plane.route[0] != '\0' ? plane.route : plane.callsign;
  if (identity[0] != '\0') {
    const int w = s_draw->textWidth(identity);
    if (w > max_w) {
      max_w = w;
    }
  }
  if (plane.type[0] != '\0') {
    const int w = s_draw->textWidth(plane.type);
    if (w > max_w) {
      max_w = w;
    }
  }
  if (plane.alt[0] != '\0') {
    const int w = s_draw->textWidth(plane.alt);
    if (w > max_w) {
      max_w = w;
    }
  }
  if (showAirlineLine(plane)) {
    const int w = s_draw->textWidth(plane.airline);
    if (w > max_w) {
      max_w = w;
    }
  }
  return max_w;
}

/** Areas already taken this frame: every aircraft symbol, then each tag. */
LabelOccupancy<2 * services::adsb::kMaxAircraft> s_label_taken;

int tagSymbolOffset() {
  return radar::kAircraftNoseLenPx + radar::kAircraftTailHalfPx +
         radar::kAircraftLabelGapPx;
}

/**
 * Draws the tag beside the symbol at (x, y). With overlap avoidance on, it
 * takes the first free spot around the symbol; when the full tag fits
 * nowhere a one-line tag (route or callsign) is tried, then no tag at all.
 */
void drawAircraftTag(int x, int y, const services::adsb::Aircraft& plane,
                     int own_symbol) {
  initTagLabelMetrics();
  applyTagStyle();

  const int line_h = s_draw->fontHeight();
  const bool airline_line = showAirlineLine(plane);
  const char* identity =
      plane.route[0] != '\0' ? plane.route : plane.callsign;
  // West (left): tag toward center on the right; east (right): on the left.
  const bool prefer_right = x < radar::kCenterX;
  const int offset = tagSymbolOffset();

  int lines = airline_line ? 4 : 3;
  int block_w = measureTagBlockWidth(plane);
  LabelRect spot = candidateRect(0, x, y, block_w, line_h * lines, offset,
                                 prefer_right, radar::kSize);
  if (services::settings::avoidTagOverlap()) {
    if (!findFreeLabelSpot(s_label_taken, x, y, block_w, line_h * lines,
                           offset, prefer_right, radar::kSize, own_symbol,
                           &spot)) {
      if (identity[0] == '\0') {
        return;
      }
      lines = 1;
      block_w = s_draw->textWidth(identity);
      if (!findFreeLabelSpot(s_label_taken, x, y, block_w, line_h, offset,
                             prefer_right, radar::kSize, own_symbol, &spot)) {
        return;
      }
    }
    s_label_taken.add(spot);
  }

  // Text hugs the symbol side: left-aligned when the tag is to the right.
  const bool tag_right_of_symbol = spot.x >= x;
  int anchor_x = spot.x;
  if (tag_right_of_symbol) {
    s_draw->setTextDatum(textdatum_t::top_left);
  } else {
    anchor_x = spot.x + spot.w;
    s_draw->setTextDatum(textdatum_t::top_right);
  }
  int ly = spot.y;

  if (identity[0] != '\0') {
    s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
    s_draw->drawString(identity, anchor_x, ly);
  }
  if (lines == 1) {
    return;
  }
  ly += line_h;

  if (plane.type[0] != '\0') {
    s_draw->setTextColor(radar::kColorTagType, radar::kColorBackground);
    s_draw->drawString(plane.type, anchor_x, ly);
  }
  ly += line_h;

  if (plane.alt[0] != '\0') {
    s_draw->setTextColor(radar::kColorTagAltitude, radar::kColorBackground);
    s_draw->drawString(plane.alt, anchor_x, ly);
  }
  ly += line_h;

  if (airline_line) {
    s_draw->setTextColor(radar::kColorTagAirline, radar::kColorBackground);
    s_draw->drawString(plane.airline, anchor_x, ly);
  }
}

void applyFooterStyle() {
  initFooterMetrics();
  if (s_footer_use_vlw) {
    displayFontSetSmoothSize(*s_draw,
                             s_footer_vlw_size * configuredTextScale());
  } else {
    s_draw->setFont(&lgfx_fonts::Font0);
    applyBitmapTextScale(*s_draw);
  }
}

void fitFooterText(const char* source, char* out, size_t out_len,
                   int max_width) {
  if (out_len == 0) {
    return;
  }
  strncpy(out, source != nullptr ? source : "", out_len - 1);
  out[out_len - 1] = '\0';
  if (s_draw->textWidth(out) <= max_width) {
    return;
  }

  size_t length = strlen(out);
  while (length > 3) {
    length -= 1;
    // "..." goes at cut; move it back off UTF-8 continuation bytes so a
    // multi-byte character (e.g. the degree sign) is never split.
    size_t cut = length - 3;
    while (cut > 0 && (static_cast<uint8_t>(out[cut]) & 0xC0) == 0x80) {
      --cut;
    }
    out[cut] = '.';
    out[cut + 1] = '.';
    out[cut + 2] = '.';
    out[cut + 3] = '\0';
    length = cut + 3;
    if (s_draw->textWidth(out) <= max_width) {
      return;
    }
  }
}

void drawFooterLine(const char* text, int y, int max_width, uint16_t color) {
  if (text == nullptr || text[0] == '\0') {
    return;
  }
  applyFooterStyle();
  char fitted[32] = {};
  fitFooterText(text, fitted, sizeof(fitted), max_width);
  s_draw->setTextDatum(textdatum_t::top_center);
  s_draw->setTextColor(color, radar::kColorFooterBackground);
  s_draw->drawString(fitted, radar::kCenterX, y);
}

void drawFooter() {
  if (!services::settings::footerEnabled()) {
    return;
  }

  // The trapezoid follows the narrowing bottom edge of the round panel.
  s_draw->fillTriangle(28, radar::kFooterTopY, 212, radar::kFooterTopY, 168,
                       radar::kFooterBottomY,
                       radar::kColorFooterBackground);
  s_draw->fillTriangle(28, radar::kFooterTopY, 168, radar::kFooterBottomY, 72,
                       radar::kFooterBottomY,
                       radar::kColorFooterBackground);
  s_draw->drawFastHLine(44, radar::kFooterTopY, 152, radar::kColorGrid);

  if (services::settings::weatherEnabled()) {
    char weather[32] = {};
    services::weather::formatWeatherLine(weather, sizeof(weather));
    drawFooterLine(weather, radar::kFooterWeatherY, 176,
                   radar::kColorTagType);
  }

  char date_time[20] = {};
  services::weather::formatDateTimeLine(date_time, sizeof(date_time));
  const int time_y = services::settings::weatherEnabled()
                         ? radar::kFooterTimeY
                         : radar::kFooterTimeOnlyY;
  drawFooterLine(date_time, time_y, 128,
                 radar::kColorTagAltitude);
}

/** Shown when aircraft data has stopped arriving (list already cleared). */
const char* s_banner = nullptr;

void drawNoDataLabel() {
  const char* kText = s_banner;
  if (kText == nullptr) {
    if (!services::adsb::dataStale()) {
      return;
    }
    kText = "NO DATA";
  }
  initTagLabelMetrics();
  applyTagStyle();
  const int tw = s_draw->textWidth(kText);
  const int th = s_draw->fontHeight();
  constexpr int kPadX = 4;
  constexpr int kPadY = 2;
  // Just above the center dot, clear of the crosshair labels.
  const int cy = radar::kCenterY - radar::kGridOuterRadius / 4;
  s_draw->fillRect(radar::kCenterX - tw / 2 - kPadX, cy - th / 2 - kPadY,
                   tw + kPadX * 2, th + kPadY * 2, radar::kColorBackground);
  s_draw->setTextDatum(textdatum_t::middle_center);
  s_draw->setTextColor(radar::kColorTagType, radar::kColorBackground);
  s_draw->drawString(kText, radar::kCenterX, cy);
}

struct AircraftDrawItem {
  size_t index = 0;
  int x = 0;
  int y = 0;
  int dist_sq = 0;
};

struct BeyondDotDrawItem {
  int16_t x = 0;
  int16_t y = 0;
  int dist_sq = 0;
  uint16_t color = 0;
};

/** Symbol colour and size for an aircraft's kind (Setup can turn it off). */
void aircraftStyle(const services::adsb::Aircraft& plane, uint16_t* color,
                   float* scale) {
  *color = radar::kColorAircraft;
  *scale = 1.0f;
  if (!services::settings::aircraftClassColors()) {
    return;
  }
  switch (static_cast<services::adsb::AircraftKind>(plane.kind)) {
    case services::adsb::AircraftKind::kLight:
      *color = radar::kColorLightAircraft;
      *scale = radar::kLightAircraftScale;
      break;
    case services::adsb::AircraftKind::kMilitary:
      *color = radar::kColorMilitaryAircraft;
      break;
    default:
      break;
  }
}

void sortDrawItemsFarFirst(AircraftDrawItem* items, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    const AircraftDrawItem key = items[i];
    size_t j = i;
    while (j > 0 && items[j - 1].dist_sq < key.dist_sq) {
      items[j] = items[j - 1];
      --j;
    }
    items[j] = key;
  }
}

void sortBeyondDotsFarFirst(BeyondDotDrawItem* items, size_t count) {
  for (size_t i = 1; i < count; ++i) {
    const BeyondDotDrawItem key = items[i];
    size_t j = i;
    while (j > 0 && items[j - 1].dist_sq < key.dist_sq) {
      items[j] = items[j - 1];
      --j;
    }
    items[j] = key;
  }
}

void drawAircraft() {
  initLabelMetrics();

  const size_t n = services::adsb::aircraftCount();
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();

  AircraftDrawItem items[services::adsb::kMaxAircraft];
  BeyondDotDrawItem dots[services::adsb::kMaxAircraft];
  size_t draw_count = 0;
  size_t dot_count = 0;

  for (size_t i = 0; i < n; ++i) {
    float dx_km = 0.0f;
    float dy_km = 0.0f;
    float dist_km = 0.0f;
    offsetKmFromCenter(planes[i].lat, planes[i].lon, &dx_km, &dy_km, &dist_km);

    if (isInsideOuterRingKm(dist_km)) {
      int x = 0;
      int y = 0;
      latLonToScreen(planes[i].lat, planes[i].lon, &x, &y);
      items[draw_count].index = i;
      items[draw_count].x = x;
      items[draw_count].y = y;
      items[draw_count].dist_sq = distSqFromCenter(x, y);
      ++draw_count;
      continue;
    }

    int dot_x = 0;
    int dot_y = 0;
    if (!beyondRingEdgeDotFromLatLon(planes[i].lat, planes[i].lon, &dot_x,
                                     &dot_y)) {
      continue;
    }
    float dot_scale = 1.0f;
    dots[dot_count].x = static_cast<int16_t>(dot_x);
    dots[dot_count].y = static_cast<int16_t>(dot_y);
    dots[dot_count].dist_sq = distSqFromCenter(dot_x, dot_y);
    aircraftStyle(planes[i], &dots[dot_count].color, &dot_scale);
    ++dot_count;
  }

  sortBeyondDotsFarFirst(dots, dot_count);
  for (size_t d = 0; d < dot_count; ++d) {
    drawBeyondRingDot(dots[d].x, dots[d].y, dots[d].color);
  }

  sortDrawItemsFarFirst(items, draw_count);
  for (size_t d = 0; d < draw_count; ++d) {
    const size_t i = items[d].index;
    const int x = items[d].x;
    const int y = items[d].y;
    uint16_t color = 0;
    float scale = 1.0f;
    aircraftStyle(planes[i], &color, &scale);
    const float nose = radar::screenAngleDeg(planes[i].nose_deg);
    const float track = radar::screenAngleDeg(planes[i].track_deg);
    drawSpeedVector(x, y, nose, track, planes[i].gs_knots,
                    radar::kColorTrackVector, scale);
    drawHeadingTriangle(x, y, nose, color, scale);
  }
  // Tags: every symbol counts as taken, then tags are placed nearest the
  // centre first so the closest aircraft keep their full tags.
  s_label_taken.clear();
  for (size_t d = 0; d < draw_count; ++d) {
    float scale = 1.0f;
    uint16_t color = 0;
    aircraftStyle(planes[items[d].index], &color, &scale);
    // The nose length covers the triangle whichever way it points.
    const int half =
        static_cast<int>(radar::kAircraftNoseLenPx * scale + 0.5f);
    LabelRect symbol;
    symbol.x = static_cast<int16_t>(items[d].x - half);
    symbol.y = static_cast<int16_t>(items[d].y - half);
    symbol.w = static_cast<int16_t>(2 * half + 1);
    symbol.h = static_cast<int16_t>(2 * half + 1);
    s_label_taken.add(symbol);
  }
  for (size_t d = draw_count; d-- > 0;) {
    const size_t i = items[d].index;
    drawAircraftTag(items[d].x, items[d].y, planes[i], static_cast<int>(d));
  }
}

void applyCardinalStyle() {
  if (s_cardinal_use_vlw) {
    displayFontSetSmoothSize(*s_draw,
                             s_cardinal_vlw_size * configuredTextScale());
  } else {
    displayFontSetBitmap(*s_draw, s_cardinal_gfx);
    applyBitmapTextScale(*s_draw);
  }
}

void applyScaleStyle() {
  if (s_scale_use_vlw) {
    displayFontSetSmoothSize(*s_draw,
                             s_scale_vlw_size * configuredTextScale());
  } else {
    displayFontSetBitmap(*s_draw, s_scale_gfx);
    applyBitmapTextScale(*s_draw);
  }
}

void drawCardinalLabel(const char* text, int x, int y, textdatum_t datum) {
  applyCardinalStyle();
  s_draw->setTextDatum(datum);
  s_draw->setTextColor(radar::kColorLabel, radar::kColorBackground);
  s_draw->drawString(text, x, y);
}

void drawScaleLabelWithBackground(const char* text, int x, int y) {
  applyScaleStyle();
  s_draw->setTextDatum(textdatum_t::middle_right);

  const int tw = s_draw->textWidth(text);
  const int th = s_draw->fontHeight();
  constexpr int kPadX = 3;
  constexpr int kPadY = 2;

  const int left = x - tw - kPadX;
  const int top = y - th / 2 - kPadY;

  s_draw->fillRect(left, top, tw + kPadX * 2, th + kPadY * 2,
                   radar::kColorBackground);
  s_draw->setTextColor(radar::kColorGrid, radar::kColorBackground);
  s_draw->drawString(text, x, y);
}

void drawGridRing(int cx, int cy, int r, uint16_t color) {
  if (r <= 0) {
    return;
  }
  const int thickness =
      std::max(1, static_cast<int>(radar::kGridStrokeHalfWidth * 2.0f));
  // False positive: r - i reaches 0 when r == 1 and thickness is 2.
  // cppcheck-suppress knownConditionTrueFalse
  for (int i = 0; i < thickness && r - i > 0; ++i) {
    s_draw->drawCircle(cx, cy, r - i, color);
  }
}

void drawRings(int cx, int cy, int outer_radius) {
  for (int i = 1; i <= radar::kRingCount; ++i) {
    const int r = (outer_radius * i) / radar::kRingCount;
    drawGridRing(cx, cy, r, radar::kColorGrid);
  }
}

/** North-south and east-west lines, turned with the compass rotation. */
void drawCrosshairs(int cx, int cy, int radius, uint16_t color) {
  constexpr float kDegToRad = 0.01745329252f;
  const float rad = radar::screenAngleDeg(0.0f) * kDegToRad;
  const int nx = static_cast<int>(lroundf(sinf(rad) * radius));
  const int ny = static_cast<int>(lroundf(cosf(rad) * radius));
  // North end at (cx + nx, cy - ny); east is 90 degrees clockwise.
  s_draw->drawWideLine(cx + nx, cy - ny, cx - nx, cy + ny,
                       radar::kGridStrokeHalfWidth, color);
  s_draw->drawWideLine(cx + ny, cy + nx, cx - ny, cy - nx,
                       radar::kGridStrokeHalfWidth, color);
}

void drawCenterDot(int cx, int cy) {
  s_draw->fillSmoothCircle(cx, cy, radar::kCenterDotRadius, radar::kColorCenter);
}

void drawCardinalLabels() {
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int edge = radar::kSize - 1;

  if (radar::upBearingDeg() != 0) {
    // Rotated: centre each letter on its bearing, just inside the edge.
    constexpr float kDegToRad = 0.01745329252f;
    static constexpr const char* kNames[] = {"N", "E", "S", "W"};
    const float r = static_cast<float>(cx - radar::kCardinalRotatedInsetPx);
    for (int i = 0; i < 4; ++i) {
      const float rad = radar::screenAngleDeg(i * 90.0f) * kDegToRad;
      drawCardinalLabel(kNames[i], cx + static_cast<int>(lroundf(sinf(rad) * r)),
                        cy - static_cast<int>(lroundf(cosf(rad) * r)),
                        textdatum_t::middle_center);
    }
    return;
  }

  drawCardinalLabel("N", cx, radar::kCardinalNorthOffsetY, textdatum_t::top_center);
  drawCardinalLabel("S", cx, edge + radar::kCardinalSouthOffsetY,
                    textdatum_t::bottom_center);
  drawCardinalLabel("W", 0, cy, textdatum_t::middle_left);
  drawCardinalLabel("E", edge, cy, textdatum_t::middle_right);
}

int scaleLabelAnchorX(int cx, int outer_radius) {
  return cx + outer_radius - radar::kScaleGapFromOuterRing;
}

void drawScaleLabel(int cx, int cy, int outer_radius) {
  char scale_label[12];
  radar::formatCurrentRing3Label(scale_label, sizeof(scale_label));
  drawScaleLabelWithBackground(scale_label,
                               scaleLabelAnchorX(cx, outer_radius), cy);
}

template <typename Gfx>
void drawStaticGrid(Gfx& gfx) {
  initLabelMetrics();
  const DrawScope scope(gfx);
  displayFontEnsureLoaded(gfx);
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int grid_r = radar::kGridOuterRadius;

  gfx.fillScreen(radar::kColorBackground);
  drawRings(cx, cy, grid_r);
  drawCrosshairs(cx, cy, grid_r, radar::kColorGrid);
  initPalette();
  runway::drawLocalAirports(gfx);  // under the large airports
  runway::drawLargeAirportRunways(gfx);
  drawCenterDot(cx, cy);
  drawCardinalLabels();
  drawScaleLabel(cx, cy, grid_r);
  gfx.setTextDatum(textdatum_t::top_left);
}

unsigned long s_frame_alloc_failures = 0;

bool ensureFrameSprite() {
  if (s_frame_ready) {
    return true;
  }
  // 8-bit (RGB332) frame: 57.6 KB instead of 115 KB at 16-bit. Colours are
  // converted when drawn and when pushed.
  s_frame.setColorDepth(8);
  if (!s_frame.createSprite(radar::kSize, radar::kSize)) {
    // The frame is allocated per redraw (see renderFrame), so this means the
    // heap is fragmented. The fallback draws straight to the panel.
    ++s_frame_alloc_failures;
    Serial.printf("radar: frame alloc failed (%lu so far), largest block %u\n",
                  s_frame_alloc_failures,
                  static_cast<unsigned>(heap_caps_get_largest_free_block(
                      MALLOC_CAP_8BIT)));
    return false;
  }
  s_frame_ready = true;
  return true;
}

// Double-buffered frame: composite the grid AND aircraft into the off-screen
// sprite, then blit it to the panel in a single pushSprite. Because the panel
// is updated in one pass, labels never show an erase/redraw gap — no flicker.
void renderFrame() {
  drawStaticGrid(s_frame);  // opens its own DrawScope(s_frame)
  {
    const DrawScope scope(s_frame);
    drawAircraft();
    drawNoDataLabel();
    drawFooter();
  }
  s_frame.pushSprite(0, 0);
  tft.setTextDatum(textdatum_t::top_left);
  // The panel keeps the image, so the buffer is only needed while drawing.
  // Redraws happen only in loop(), never during an HTTPS request, so freeing
  // it here hands its 57.6 KB to each request (one costs 57-66 KB, and the
  // heap low point was ~20 KB with the frame kept, ~12 KB while the
  // airports downloaded).
  s_frame.deleteSprite();
  s_frame_ready = false;
}

}  // namespace

namespace {
bool s_screen_on = true;
}  // namespace

void radarDisplaySetScreenOn(bool on) {
  if (on == s_screen_on) {
    return;
  }
  s_screen_on = on;
  if (on) {
    tft.wakeup();
    Serial.println("Screen on");
  } else {
    tft.fillScreen(TFT_BLACK);
    tft.sleep();
    Serial.println("Screen off (night)");
  }
}

bool radarDisplayScreenOn() { return s_screen_on; }

void radarDisplaySetBanner(const char* text) { s_banner = text; }

void radarDisplayDraw() {
  if (!s_screen_on) {
    return;
  }
  initPalette();
  initLabelMetrics();

  if (ensureFrameSprite()) {
    renderFrame();
    return;
  }

  // Fallback when the sprite can't be allocated: draw straight to the panel.
  const DrawScope scope(tft);
  drawStaticGrid(tft);
  drawAircraft();
  drawNoDataLabel();
  drawFooter();
  tft.setTextDatum(textdatum_t::top_left);
}

void radarDisplayRefreshAircraft() {
  if (!s_screen_on) {
    return;
  }
  initPalette();

  if (ensureFrameSprite()) {
    renderFrame();
    return;
  }

  radarDisplayDraw();
}

}  // namespace ui
