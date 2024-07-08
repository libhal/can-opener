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

#include <app/hardware_map.hpp>

hardware_map_t initialize_platform()
{
  using namespace hal::literals;

  hal::micromod::v1::initialize_platform();
  auto& console = hal::micromod::v1::console(hal::buffer<128>);
  console.configure(hal::serial::settings{
    .baud_rate = 115200,
    .stop = hal::serial::settings::stop_bits::one,
    .parity = hal::serial::settings::parity::none,
  });

  return {
    .red_led = &hal::micromod::v1::led(),
    .console = &console,
    .clock = &hal::micromod::v1::uptime_clock(),
    .can = &hal::micromod::v1::can(),
    .reset = +[]() { hal::micromod::v1::reset(); },
  };
}
