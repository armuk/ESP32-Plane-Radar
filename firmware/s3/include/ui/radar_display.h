#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/**
 * Night screen-off. Off blanks and sleeps the panel and skips radar drawing;
 * on wakes it (the caller redraws). Without a backlight pin the backlight
 * stays lit, so "off" is a black screen.
 */
void radarDisplaySetScreenOn(bool on);

/** Centre message (e.g. "UPDATING AIRPORTS"); nullptr clears it. */
void radarDisplaySetBanner(const char* text);
bool radarDisplayScreenOn();

}  // namespace ui
