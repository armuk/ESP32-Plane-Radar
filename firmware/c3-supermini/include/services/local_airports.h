#pragma once

#include <cstddef>
#include <cstdint>

#include "services/airport_area.h"

/**
 * Medium airports near the radar location, downloaded from
 * OurAirports when the location changes and kept in the flash file system
 * (the otherwise unused "spiffs" partition). Large airports stay built in.
 */
namespace services::airports {

/** Mount the file system and read the saved area header. */
void init();

/** Hook called during the download (e.g. BOOT button polling). */
using PollFn = void (*)();
void setPollFn(PollFn fn);

/**
 * True when a download is due: no saved data, or the radar has moved more
 * than a few km from the saved area (retried every 15 minutes after a
 * failure).
 */
bool refreshDue(double lat, double lon);

/**
 * Downloads and saves the area around lat/lon. Blocks, typically a minute or
 * two. Returns true when new data was saved.
 */
bool refresh(double lat, double lon);

/** True while the saved data covers the given location. */
bool hasDataFor(double lat, double lon);

uint16_t airportCount();
uint16_t runwayCount();

/** One line for the Setup page, e.g. "25 medium airports saved ...". */
void formatStatus(char* out, size_t out_len);

// --- Reading the saved records (one open file at a time) ---

bool beginRead();
bool readAirport(uint16_t index, LocalAirport* out);
bool readRunway(uint16_t index, LocalRunway* out);
void endRead();

}  // namespace services::airports
