#include "ui/runway_overlay.h"

#include <lgfx/v1/lgfx_fonts.hpp>

#include <cmath>
#include <cstdlib>

#include "data/large_airports.h"
#include "hardware/display_font.h"
#include "services/display_settings.h"
#include "services/local_airports.h"
#include "services/radar_location.h"
#include "ui/radar_projection.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace lgfx_fonts = lgfx::v1::fonts;

namespace ui::runway {
namespace {

constexpr size_t kMaxAirportLabels = 32;

/** Per-frame airport state, so each airport is measured at most once. */
enum class AirportRange : uint8_t { kUnknown, kInRange, kOutOfRange };
AirportRange s_range_state[data::large_airports::kAirportCount];
bool s_label_pending[data::large_airports::kAirportCount];

bool s_runway_label_ready = false;
bool s_runway_label_use_vlw = false;
float s_runway_label_vlw_size = 0.38f;
const lgfx::GFXfont* s_runway_label_gfx =
    &lgfx_fonts::FreeSansBold12pt7b;

int measureVlwHeight(lgfx::LGFXBase& gfx, float size) {
  gfx.setTextSize(size);
  return gfx.fontHeight();
}

float findVlwSizeForHeight(lgfx::LGFXBase& gfx, int target_px) {
  float lo = 0.2f;
  float hi = 1.2f;
  for (int i = 0; i < 14; ++i) {
    const float mid = (lo + hi) * 0.5f;
    if (measureVlwHeight(gfx, mid) < target_px) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return hi;
}

void initRunwayLabelStyle(lgfx::LGFXBase& gfx) {
  if (s_runway_label_ready) {
    return;
  }

  const int target = radar::kRunwayLabelHeightPx;
  if (displayFontIsSmooth()) {
    s_runway_label_use_vlw = true;
    s_runway_label_vlw_size = findVlwSizeForHeight(gfx, target);
  } else {
    s_runway_label_gfx = &lgfx_fonts::FreeSansBold12pt7b;
    s_runway_label_use_vlw = false;
  }
  s_runway_label_ready = true;
}

void applyRunwayLabelStyle(lgfx::LGFXBase& gfx) {
  const float text_scale =
      static_cast<float>(services::settings::textScalePercent()) / 100.0f;
  if (s_runway_label_use_vlw) {
    displayFontSetSmoothSize(gfx, s_runway_label_vlw_size * text_scale);
  } else {
    displayFontSetBitmap(gfx, s_runway_label_gfx);
    gfx.setTextSize(text_scale);
  }
}

float e7ToDeg(int32_t e7) { return static_cast<float>(e7) * 1e-7f; }

using radar::latLonToScreen;
using radar::offsetKmFromCenter;

int distSqFromCenter(int x, int y) {
  const int dx = x - radar::kCenterX;
  const int dy = y - radar::kCenterY;
  return dx * dx + dy * dy;
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

bool segmentIntersectsDisc(int x0, int y0, int x1, int y1) {
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int r = radar::kGridOuterRadius;
  const int r_sq = r * r;

  if (distSqFromCenter(x0, y0) <= r_sq || distSqFromCenter(x1, y1) <= r_sq) {
    return true;
  }

  const int dx = x1 - x0;
  const int dy = y1 - y0;
  const int fx = x0 - cx;
  const int fy = y0 - cy;
  // Float: at small ranges b*b passes 2^31 in int and the sign flips.
  const float a = static_cast<float>(dx * dx + dy * dy);
  if (a == 0.0f) {
    return false;
  }
  const float b = 2.0f * static_cast<float>(fx * dx + fy * dy);
  const float c = static_cast<float>(fx * fx + fy * fy - r_sq);
  const float disc_sq = b * b - 4.0f * a * c;
  if (disc_sq < 0.0f) {
    return false;
  }
  const float disc = sqrtf(disc_sq);
  const float inv2a = 1.0f / (2.0f * a);
  const float t0 = (-b - disc) * inv2a;
  const float t1 = (-b + disc) * inv2a;
  return (t0 >= 0.0f && t0 <= 1.0f) || (t1 >= 0.0f && t1 <= 1.0f);
}

void drawBoldRunwayLabel(lgfx::LGFXBase& gfx, const char* ident, int mx,
                         int my, uint16_t color, bool bold = true) {
  const int tw = gfx.textWidth(ident);
  const int th = gfx.fontHeight();
  constexpr int kPadX = 2;
  constexpr int kPadY = 1;

  gfx.setTextDatum(textdatum_t::bottom_center);
  const int left = mx - tw / 2 - kPadX;
  const int top = my - th - kPadY;
  gfx.fillRect(left, top, tw + kPadX * 2, th + kPadY, radar::kColorBackground);
  gfx.setTextColor(color, radar::kColorBackground);
  if (bold) {
    gfx.drawString(ident, mx - 1, my);
    gfx.drawString(ident, mx + 1, my);
  }
  gfx.drawString(ident, mx, my);
}

bool drawRunwayLine(lgfx::LGFXBase& gfx, int32_t le_lat_e7, int32_t le_lon_e7,
                    int32_t he_lat_e7, int32_t he_lon_e7, float half_width,
                    uint16_t color) {
  const float le_lat = e7ToDeg(le_lat_e7);
  const float le_lon = e7ToDeg(le_lon_e7);
  const float he_lat = e7ToDeg(he_lat_e7);
  const float he_lon = e7ToDeg(he_lon_e7);

  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  latLonToScreen(le_lat, le_lon, &x0, &y0);
  latLonToScreen(he_lat, he_lon, &x1, &y1);

  if (!segmentIntersectsDisc(x0, y0, x1, y1)) {
    return false;
  }

  clipPointToOuterRing(x0, y0, &x1, &y1);
  clipPointToOuterRing(x1, y1, &x0, &y0);

  gfx.drawWideLine(x0, y0, x1, y1, half_width, color);
  return true;
}

bool drawRunwayLine(lgfx::LGFXBase& gfx, const data::large_airports::Runway& rw) {
  return drawRunwayLine(gfx, rw.le_lat_e7, rw.le_lon_e7, rw.he_lat_e7,
                        rw.he_lon_e7, radar::kRunwayLineHalfWidth,
                        radar::kColorRunway);
}

void offsetLabelFromCenter(int ax, int ay, int* lx, int* ly) {
  const int dx = ax - radar::kCenterX;
  const int dy = ay - radar::kCenterY;
  const float len = sqrtf(static_cast<float>(dx * dx + dy * dy));
  const int gap = radar::kRunwayLabelGapPx;
  if (len < 1.0f) {
    *lx = ax;
    *ly = ay - gap;
    return;
  }
  *lx = ax + static_cast<int>(lroundf(dx / len * static_cast<float>(gap)));
  *ly = ay + static_cast<int>(lroundf(dy / len * static_cast<float>(gap)));
}

void clipPointOntoOuterRing(int* x, int* y) {
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int r = radar::kGridOuterRadius;
  const int dx = *x - cx;
  const int dy = *y - cy;
  const int d_sq = dx * dx + dy * dy;
  const int r_sq = r * r;
  if (d_sq <= r_sq || d_sq == 0) {
    return;
  }
  const float scale = static_cast<float>(r) / sqrtf(static_cast<float>(d_sq));
  *x = cx + static_cast<int>(lroundf(static_cast<float>(dx) * scale));
  *y = cy + static_cast<int>(lroundf(static_cast<float>(dy) * scale));
}

void drawAirportLabel(lgfx::LGFXBase& gfx, int32_t lat_e7, int32_t lon_e7,
                      const char* ident, uint16_t color, bool bold) {
  int ax = 0;
  int ay = 0;
  latLonToScreen(e7ToDeg(lat_e7), e7ToDeg(lon_e7), &ax, &ay);
  clipPointOntoOuterRing(&ax, &ay);

  int lx = 0;
  int ly = 0;
  offsetLabelFromCenter(ax, ay, &lx, &ly);
  drawBoldRunwayLabel(gfx, ident, lx, ly, color, bold);
}

void drawAirportLabel(lgfx::LGFXBase& gfx,
                      const data::large_airports::Airport& ap) {
  drawAirportLabel(gfx, ap.lat_e7, ap.lon_e7, ap.ident,
                   radar::kColorRunwayLabel, true);
}

// Per-frame state for the downloaded airports (1 byte each).
enum LocalFlags : uint8_t { kLocalInRange = 1, kLocalHasRunway = 2 };
uint8_t s_local_flags[services::airports::kMaxLocalAirports];



}  // namespace

/**
 * Square around the range in the table's 1e-7 degree units. Airports
 * outside it are skipped with integer compares, so only the few nearby ones
 * pay for the float distance (cosf/sqrtf are software on the ESP32-C3).
 */
struct E7Box {
  int64_t center_lat_e7;
  int64_t center_lon_e7;
  int64_t half_lat_e7;
  int64_t half_lon_e7;  // < 0: no longitude limit (near the poles)
};

constexpr int64_t kE7FullCircle = 3600000000LL;

E7Box rangeBoxE7(float radius_km) {
  constexpr double kKmPerDeg = 111.0;  // matches radar_projection.cpp
  constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
  // 5% plus 1 km of slack: the box only has to contain the circle.
  const double half_km = radius_km * 1.05 + 1.0;
  const double center_lat = services::location::lat();
  E7Box box = {};
  box.center_lat_e7 = llround(center_lat * 1e7);
  box.center_lon_e7 = llround(services::location::lon() * 1e7);
  box.half_lat_e7 = llround(half_km / kKmPerDeg * 1e7);
  const double cos_lat = cos(center_lat * kDegToRad);
  const double half_lon_deg = cos_lat > 0.01 ? half_km / (kKmPerDeg * cos_lat)
                                             : 360.0;
  box.half_lon_e7 = half_lon_deg < 180.0 ? llround(half_lon_deg * 1e7) : -1;
  return box;
}

bool insideBoxE7(const E7Box& box, int32_t lat_e7, int32_t lon_e7) {
  if (llabs(lat_e7 - box.center_lat_e7) > box.half_lat_e7) {
    return false;
  }
  if (box.half_lon_e7 < 0) {
    return true;
  }
  int64_t dlon = lon_e7 - box.center_lon_e7;
  if (dlon > kE7FullCircle / 2) {
    dlon -= kE7FullCircle;  // short way round the antimeridian
  } else if (dlon < -kE7FullCircle / 2) {
    dlon += kE7FullCircle;
  }
  return llabs(dlon) <= box.half_lon_e7;
}

void drawLargeAirportRunways(lgfx::LGFXBase& gfx) {
  if (!radar::showRunways()) {
    return;
  }
  displayFontEnsureLoaded(gfx);
  const float radius_km = radar::fetchRadiusKm();
  const E7Box box = rangeBoxE7(radius_km);

  uint16_t label_airports[kMaxAirportLabels];
  size_t label_count = 0;

  for (size_t i = 0; i < data::large_airports::kAirportCount; ++i) {
    s_range_state[i] = AirportRange::kUnknown;
    s_label_pending[i] = false;
  }

  for (size_t i = 0; i < data::large_airports::kRunwayCount; ++i) {
    const auto& rw = data::large_airports::kRunways[i];
    const uint16_t ap_idx = rw.airport_idx;
    AirportRange& state = s_range_state[ap_idx];
    if (state == AirportRange::kUnknown) {
      const auto& ap = data::large_airports::kAirports[ap_idx];
      bool in_range = false;
      if (insideBoxE7(box, ap.lat_e7, ap.lon_e7)) {
        float dx_km = 0.0f;
        float dy_km = 0.0f;
        float dist_km = 0.0f;
        offsetKmFromCenter(e7ToDeg(ap.lat_e7), e7ToDeg(ap.lon_e7), &dx_km,
                           &dy_km, &dist_km);
        in_range = dist_km <= radius_km;
      }
      state = in_range ? AirportRange::kInRange : AirportRange::kOutOfRange;
    }
    if (state != AirportRange::kInRange) {
      continue;
    }
    if (!drawRunwayLine(gfx, rw)) {
      continue;
    }
    if (!s_label_pending[ap_idx] && label_count < kMaxAirportLabels) {
      s_label_pending[ap_idx] = true;
      label_airports[label_count++] = ap_idx;
    }
  }

  if (label_count == 0) {
    return;
  }

  initRunwayLabelStyle(gfx);
  applyRunwayLabelStyle(gfx);
  for (size_t i = 0; i < label_count; ++i) {
    drawAirportLabel(gfx, data::large_airports::kAirports[label_airports[i]]);
  }
}

void drawLocalAirports(lgfx::LGFXBase& gfx) {
  namespace apt = services::airports;
  if (!radar::showRunways() || !services::settings::showMediumAirports() ||
      !apt::beginRead()) {
    return;
  }
  const float radius_km = radar::fetchRadiusKm();
  const E7Box box = rangeBoxE7(radius_km);
  const uint16_t airports = apt::airportCount();
  const uint16_t runways = apt::runwayCount();

  size_t in_range = 0;
  for (uint16_t i = 0; i < airports; ++i) {
    s_local_flags[i] = 0;
    apt::LocalAirport ap = {};
    if (!apt::readAirport(i, &ap) || !insideBoxE7(box, ap.lat_e7, ap.lon_e7)) {
      continue;
    }
    float dx_km = 0.0f;
    float dy_km = 0.0f;
    float dist_km = 0.0f;
    offsetKmFromCenter(e7ToDeg(ap.lat_e7), e7ToDeg(ap.lon_e7), &dx_km,
                       &dy_km, &dist_km);
    if (dist_km <= radius_km) {
      s_local_flags[i] = kLocalInRange;
      ++in_range;
    }
  }

  if (in_range > 0) {
    for (uint16_t r = 0; r < runways; ++r) {
      apt::LocalRunway rw = {};
      if (!apt::readRunway(r, &rw) || rw.airport_idx >= airports ||
          (s_local_flags[rw.airport_idx] & kLocalInRange) == 0) {
        continue;
      }
      drawRunwayLine(gfx, rw.le_lat_e7, rw.le_lon_e7, rw.he_lat_e7,
                     rw.he_lon_e7, radar::kMediumRunwayHalfWidth,
                     radar::kColorRunway);
      s_local_flags[rw.airport_idx] |= kLocalHasRunway;
    }

    // A dot for airports without runway data, then the labels on top.
    size_t labels = 0;
    initRunwayLabelStyle(gfx);
    applyRunwayLabelStyle(gfx);
    const int ring_sq = radar::kGridOuterRadius * radar::kGridOuterRadius;
    for (uint16_t i = 0; i < airports && labels < kMaxAirportLabels; ++i) {
      apt::LocalAirport ap = {};
      if ((s_local_flags[i] & kLocalInRange) == 0 ||
          !apt::readAirport(i, &ap)) {
        continue;
      }
      int ax = 0;
      int ay = 0;
      latLonToScreen(e7ToDeg(ap.lat_e7), e7ToDeg(ap.lon_e7), &ax, &ay);
      if (distSqFromCenter(ax, ay) > ring_sq) {
        continue;
      }
      if ((s_local_flags[i] & kLocalHasRunway) == 0) {
        gfx.fillCircle(ax, ay, radar::kAirportDotRadiusPx, radar::kColorRunway);
      }
      ap.label[sizeof(ap.label) - 1] = '\0';
      drawAirportLabel(gfx, ap.lat_e7, ap.lon_e7, ap.label,
                       radar::kColorRunwayLabel, true);
      ++labels;
    }
  }
  apt::endRead();
}

}  // namespace ui::runway
