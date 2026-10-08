// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Core {
class System;
}

// Lowers the emulated CPU clock while the core sees a movie playing (MovieLib
// loaded, or Y2R converting in bursts), off unless the option asks for it.
// From raikopon by way of dekopon.
namespace SwitchFrontend::MovieThrottle {

// Before the game loads: follows the core's movie playback from then on.
void Register(Core::System& system);

// The option and the clock (percent) to drop to; takes effect at once.
void Configure(bool enabled, int percentage);

// After the settings put the CPU clock back (System::ApplySettings): lowers it
// again if a movie is still playing.
void Reapply(Core::System& system);

} // namespace SwitchFrontend::MovieThrottle
