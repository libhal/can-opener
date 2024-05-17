#include <chrono>

#include <libhal-exceptions/control.hpp>
#include <libhal-util/serial.hpp>
#include <libhal-util/steady_clock.hpp>
#include <libhal/error.hpp>
#include <libhal/steady_clock.hpp>

#include <app/hardware_map.hpp>

hardware_map_t hardware_map{};

int main()
{
  using namespace std::chrono_literals;

  try {
    hardware_map = initialize_platform();
  } catch (...) {
    hal::halt();
  }

  hal::set_terminate(+[]() { hardware_map.reset(); });

  auto& led = *hardware_map.led;
  auto& clock = *hardware_map.clock;
  auto& console = *hardware_map.console;
  auto& can = *hardware_map.can;

  can.on_receive([&console, &led](const hal::can::message_t& p_message) {
    hal::print<128>(console, "NEW MESSAGE! 0x%04X", p_message.id);
  });

  while (true) {
    led.level(!led.level());
    hal::delay(clock, 500ms);
  }

  return 0;
}
