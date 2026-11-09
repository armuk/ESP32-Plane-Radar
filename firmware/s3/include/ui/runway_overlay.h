#pragma once

#include <LovyanGFX.hpp>

namespace ui::runway {

void drawLargeAirportRunways(lgfx::LGFXBase& gfx);

/** Medium airports downloaded for the radar's area (if any). */
void drawLocalAirports(lgfx::LGFXBase& gfx);

}  // namespace ui::runway
