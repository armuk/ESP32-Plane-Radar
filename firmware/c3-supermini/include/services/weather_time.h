#pragma once

#include <cstddef>

namespace services::weather {

using PollFn = void (*)();

/** Start UTC NTP synchronization. Safe to call after every reconnect. */
void begin();
/** Re-apply settings::ntpServer() after it changes on the Setup page. */
void restartTimeSync();
/** Apply settings::timeZone() (or UTC when automatic) to the C library. */
void applyTimeZone();
void setPollFn(PollFn fn);

/**
 * Refresh current conditions and the location's UTC offset when due.
 * Returns true only when displayable data changed.
 */
bool refreshIfDue(double latitude, double longitude, bool force = false);

bool valid();
void formatWeatherLine(char* out, size_t out_len);
void formatDateTimeLine(char* out, size_t out_len);

/**
 * Local hour (0..23) using the same time zone as the footer clock. False
 * until the clock is set (NTP or the weather response).
 */
bool localHour(int* hour);

}  // namespace services::weather
