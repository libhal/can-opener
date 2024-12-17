// Copyright 2024 Khalil Estell
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <optional>

#include <libhal/can.hpp>
#include <libhal/functional.hpp>
#include <libhal/output_pin.hpp>
#include <libhal/serial.hpp>
#include <libhal/steady_clock.hpp>

struct resource_list
{
  std::optional<hal::output_pin*> red_led;
  std::optional<hal::serial*> console;
  std::optional<hal::steady_clock*> clock;
  std::optional<hal::can_transceiver*> can_transceiver;
  std::optional<hal::can_bus_manager*> can_bus_manager;
  std::optional<hal::can_interrupt*> can_interrupt;
  std::optional<hal::can_extended_mask_filter*> can_mask_filter;
  std::optional<hal::callback<void()>> reset;
};

// Application function must be implemented by one of the compilation units
// (.cpp) files.
void initialize_processor();
void initialize_platform(resource_list& p_map);
