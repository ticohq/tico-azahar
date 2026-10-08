// Copyright 2026 Azahar Emulator Project
// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Boost mode: the CPU at 1785 MHz and the GPU at 768 MHz while the game runs.
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

} // namespace SwitchFrontend::Clocks
