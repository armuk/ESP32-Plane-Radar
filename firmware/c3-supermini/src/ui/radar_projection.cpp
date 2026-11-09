#include "ui/radar_projection.h"

#include <cmath>

#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace ui::radar {
namespace {

constexpr double kKmPerDegLat = 111.0;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

/** cos(center latitude) is cached; the center only changes from the portal. */
double s_cached_center_lat = NAN;
double s_cached_km_per_deg_lon = kKmPerDegLat;

int s_up_bearing_deg = 0;
float s_up_sin = 0.0f;
float s_up_cos = 1.0f;

double kmPerDegLon(double center_lat) {
  if (center_lat != s_cached_center_lat) {
    s_cached_center_lat = center_lat;
    s_cached_km_per_deg_lon = kKmPerDegLat * cos(center_lat * kDegToRad);
  }
  return s_cached_km_per_deg_lon;
}

}  // namespace

void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km,
                        float* dist_km) {
  const double center_lat = services::location::lat();
  double dlon = static_cast<double>(lon) - services::location::lon();
  // Take the short way around near the antimeridian.
  if (dlon > 180.0) {
    dlon -= 360.0;
  } else if (dlon < -180.0) {
    dlon += 360.0;
  }

  *dx_km = static_cast<float>(dlon * kmPerDegLon(center_lat));
  *dy_km = static_cast<float>((static_cast<double>(lat) - center_lat) *
                              kKmPerDegLat);
  *dist_km = sqrtf((*dx_km) * (*dx_km) + (*dy_km) * (*dy_km));
}

void setUpBearingDeg(int bearing_deg) {
  bearing_deg %= 360;
  if (bearing_deg < 0) {
    bearing_deg += 360;
  }
  s_up_bearing_deg = bearing_deg;
  const double rad = bearing_deg * kDegToRad;
  s_up_sin = static_cast<float>(sin(rad));
  s_up_cos = static_cast<float>(cos(rad));
}

int upBearingDeg() { return s_up_bearing_deg; }

float screenAngleDeg(float bearing_deg) {
  return bearing_deg - static_cast<float>(s_up_bearing_deg);
}

void rotateToScreen(float east_km, float north_km, float* right_km,
                    float* up_km) {
  // A point at bearing b appears at screen angle b - up.
  *right_km = east_km * s_up_cos - north_km * s_up_sin;
  *up_km = north_km * s_up_cos + east_km * s_up_sin;
}

void latLonToScreen(float lat, float lon, int* out_x, int* out_y) {
  const float outer_km = rangeCurrent().outer_km;
  const float px_per_km = static_cast<float>(kGridOuterRadius) / outer_km;

  float dx_km = 0.0f;
  float dy_km = 0.0f;
  float dist_km = 0.0f;
  offsetKmFromCenter(lat, lon, &dx_km, &dy_km, &dist_km);
  float right_km = 0.0f;
  float up_km = 0.0f;
  rotateToScreen(dx_km, dy_km, &right_km, &up_km);

  *out_x = kCenterX + static_cast<int>(lroundf(right_km * px_per_km));
  *out_y = kCenterY - static_cast<int>(lroundf(up_km * px_per_km));
}

}  // namespace ui::radar
