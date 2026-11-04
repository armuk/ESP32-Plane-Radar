#pragma once

namespace ui::radar {

/**
 * Offset of lat/lon from the radar center in km (east = +x, north = +y).
 * Local equirectangular projection: longitude is scaled by cos(center
 * latitude), so east-west distances are correct away from the equator.
 */
void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km,
                        float* dist_km);

/**
 * Compass bearing shown at the top of the screen (0 = north up). Rotates
 * everything drawn through latLonToScreen() and screenAngleDeg().
 */
void setUpBearingDeg(int bearing_deg);
int upBearingDeg();

/** Turns a compass bearing into a screen angle (clockwise from up). */
float screenAngleDeg(float bearing_deg);

/**
 * Rotates an east/north offset (km) into screen right/up (km) for the
 * current up bearing.
 */
void rotateToScreen(float east_km, float north_km, float* right_km,
                    float* up_km);

/** Screen pixel for lat/lon at the active range preset and rotation. */
void latLonToScreen(float lat, float lon, int* out_x, int* out_y);

}  // namespace ui::radar
