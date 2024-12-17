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

#include <libhal-micromod/micromod.hpp>

#include <app/resource_list.hpp>

void initialize_platform(resource_list& p_map)
{
  using namespace hal::literals;
  using namespace hal::micromod;

  v1::initialize_platform();

  p_map.reset = +[]() { v1::reset(); };
  p_map.red_led = &v1::led();
  p_map.clock = &v1::uptime_clock();

  auto& console = v1::console(hal::buffer<128>);
  console.configure(hal::serial::settings{
    .baud_rate = 115200,
    .stop = hal::serial::settings::stop_bits::one,
    .parity = hal::serial::settings::parity::none,
  });

  p_map.console = &console;

  static std::array<hal::can_message, 32> can_receive_buffer{};
  p_map.can_transceiver = &v1::can_transceiver(can_receive_buffer);
  p_map.can_bus_manager = &v1::can_bus_manager();
  p_map.can_interrupt = &v1::can_interrupt();
  p_map.can_mask_filter = &v1::can_extended_mask_filter0();
}
