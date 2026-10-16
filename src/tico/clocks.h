// Copyright 2026 Azahar Emulator Project
// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Boost mode (off unless chosen): the CPU at 1785 MHz and the GPU at 768 MHz
// while the game runs. It raises a clock, never lowers it: one already faster
// (an overclock) is left as it is.
//
// With a clock manager (Horizon-OC's hoc-clk, sys-clk-OC, sys-clk) the clocks
// are asked of it, so the two don't fight; without one they are set directly.
// The clocks the console had are given back on exit.
namespace SwitchFrontend::Clocks {

void Boost();

// Every frame: Horizon puts the stock clocks back after sleep and when the
// console is docked or undocked, so they are set (or asked for) again.
void Keep();

void Restore();

// Boost while loading (on unless turned off), as dekopon does: the system's
// CPU boost profile (ApmCpuBoostMode_FastLoad: the CPU at 1785 MHz, the GPU at
// its minimum) for as long as a LoadBoostScope lives, which is while the game
// loads (System::Load). Never while the game runs, so states are saved and
// loaded at the game's own clocks. Not with Boost mode on (AllowLoadBoost
// false), nor when the CPU already runs at 1785 MHz or faster. Scopes nest;
// Restore ends it too.
void AllowLoadBoost(bool allowed);

class LoadBoostScope {
public:
    explicit LoadBoostScope(const char* why);
    ~LoadBoostScope();

    LoadBoostScope(const LoadBoostScope&) = delete;
    LoadBoostScope& operator=(const LoadBoostScope&) = delete;

private:
    bool held_ = false;
};

} // namespace SwitchFrontend::Clocks
