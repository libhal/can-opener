#include <array>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <libhal/timeout.hpp>
#include <optional>
#include <span>
#include <string_view>

#include <app/hardware_map.hpp>
#include <libhal-exceptions/control.hpp>
#include <libhal-util/as_bytes.hpp>
#include <libhal-util/serial.hpp>
#include <libhal-util/steady_clock.hpp>
#include <libhal-util/streams.hpp>
#include <libhal-util/timeout.hpp>
#include <libhal/error.hpp>
#include <libhal/steady_clock.hpp>
#include <nonstd/ring_span.hpp>

hardware_map_t hardware_map{};
std::array<hal::byte, 32> command_buffer{};
std::array<hal::can::message_t, 32> receive_buffer{};
std::array<hal::can::message_t, 32> transmit_buffer{};
nonstd::ring_span<hal::can::message_t> receive_queue(receive_buffer.begin(),
                                                     receive_buffer.end());
nonstd::ring_span<hal::can::message_t> transmit_queue(transmit_buffer.begin(),
                                                      transmit_buffer.end());
bool open;

constexpr std::string_view version = "V0000";
constexpr std::string_view serial_number = "N0000";

bool setup_command(hal::can& p_can, std::span<hal::byte> p_command)
{
  using namespace hal::literals;

  if (p_command.size() != 3 || open) {
    return false;
  }

  switch (p_command[1]) {
    case '0': {
      p_can.configure({ .baud_rate = 10.0_kHz });
      break;
    }
    case '1': {
      p_can.configure({ .baud_rate = 20.0_kHz });
      break;
    }
    case '2': {
      p_can.configure({ .baud_rate = 50.0_kHz });
      break;
    }
    case '3': {
      p_can.configure({ .baud_rate = 100.0_kHz });
      break;
    }
    case '4': {
      p_can.configure({ .baud_rate = 125.0_kHz });
      break;
    }
    case '5': {
      p_can.configure({ .baud_rate = 250.0_kHz });
      break;
    }
    case '6': {
      p_can.configure({ .baud_rate = 500.0_kHz });
      break;
    }
    case '7': {
      p_can.configure({ .baud_rate = 800.0_kHz });
      break;
    }
    case '8': {
      p_can.configure({ .baud_rate = 1.0_MHz });
      break;
    }
    default: {
      return false;
    }
  }
  return true;
}

bool open_command()
{
  // Check if open was issued while the device is already open
  if (open) {
    return false;
  }
  open = true;
  return true;
}

bool close_command()
{
  // Check if open was issued while the device is already open
  if (not open) {
    return false;
  }
  open = false;
  return true;
}

bool status_flags_command(hal::serial& p_serial)
{
  std::uint8_t status = 0x0;

  if (receive_queue.full()) {
    status |= 1 << 0;
  }

  if (transmit_queue.full()) {
    status |= 1 << 1;
  }

  // TODO(#4): Bit 2 Error warning (EI), see SJA1000
  // TODO(#5): Bit 3 Data Overrun (DOI), see SJA1000
  // TODO(#6): Bit 5 Error Passive (EPI), see SJA1000
  // TODO(#7): Bit 6 Arbitration Lost (ALI), see SJA1000
  // TODO(#8): Bit 7 Bus Error (BEI), see SJA1000

  hal::print<6>(p_serial, "F%02X\r", status);

  return true;
}

std::optional<hal::can::message_t> string_to_can_message(
  std::span<hal::byte> p_command)
{
  hal::can::message_t message{};
  std::size_t format_size = 0;
  std::size_t id_byte_length = 0;
  auto command = p_command[0];
  std::span<char> command_chars(reinterpret_cast<char*>(p_command.data()),
                                p_command.size());

  if (command == 'r' || command == 't') {
    constexpr std::string_view format = "riiil\r";
    format_size = format.size();
    id_byte_length = 3;
  } else if (command == 'R' || command == 'T') {
    constexpr std::string_view format = "Riiiiiiiil\r";
    format_size = format.size();
    id_byte_length = 8;
  }

  if (command_chars.size() < format_size) {
    return std::nullopt;
  }

  if (command == 'r' || command == 'R') {
    message.is_remote_request = true;
  } else {
    message.is_remote_request = false;
  }

  // Skip first character
  command_chars = command_chars.subspan(1);

  // Scope for status variable
  {
    auto status = std::from_chars(command_chars.data(),
                                  command_chars.data() + id_byte_length,
                                  message.id,
                                  16);

    if (status.ec != std::errc{}) {
      return std::nullopt;
    }
  }

  // Increment past ID field
  command_chars = command_chars.subspan(id_byte_length);

  // Convert length character to value
  std::size_t payload_length = command_chars[0] - '0';

  // Increment past length character
  command_chars = command_chars.subspan(1);

  bool valid_payload_length = payload_length <= 8;
  // We multiply by 2 for the payload length because it takes two characters to
  // represent each byte in the command string.
  //
  // (+ 1) for the '\r' character
  bool length_within_bounds = command_chars.size() == (payload_length * 2) + 1;

  if (not valid_payload_length || not length_within_bounds) {
    return std::nullopt;
  }

  message.length = payload_length;

  for (std::size_t i = 0; i < payload_length; i++) {
    // Every two characters is a single byte of data, so we shift it by 1
    // (divide by 2) to get the correct payload index.
    std::size_t character_offset = i * 2;
    // +2 because +1 for the next index and +1 for offset to end byte
    auto status = std::from_chars(&command_chars[character_offset],
                                  &command_chars[character_offset + 2],
                                  message.payload[i],
                                  16);
    if (status.ec != std::errc{}) {
      return std::nullopt;
    }
  }

  return message;
}

bool version_command(hal::serial& p_serial)
{
  hal::print(p_serial, version);
  return true;
}

void handle_command(hal::serial& p_serial,
                    hal::can& p_can,
                    std::span<hal::byte> p_command)
{
  using namespace std::literals;

  bool handled = false;
  if (p_command.empty()) {
    return;
  }

  // TODO(#9): Add command 'N': serial number support.
  // TODO(#10): Finish command 'V': for version number. Figure out how to add
  //            versions into the build.
  switch (p_command[0]) {
    case 'V': {
      handled = version_command(p_serial);
      break;
    }
    case '\r': {
      handled = true;
      break;
    }
  }

  if (not open) {
    // TODO(#11): Add command 'Z': timestamp control
    // TODO(#12): Add command 'M': Sets Acceptance Code Register
    // TODO(#13): Add command 'm': Sets Acceptance Mask Register
    // TODO(#14): Add command 's': Setup with BTR0/BTR1 CAN bit-rates
    switch (p_command[0]) {
      case 'S': {
        handled = setup_command(p_can, p_command);
        break;
      }
      case 'O': {
        handled = open_command();
        break;
      }
    }
  } else {
    switch (p_command[0]) {
      case 'C': {
        handled = close_command();
        break;
      }
      case 'F': {
        handled = status_flags_command(p_serial);
        break;
      }
      case 't':
      case 'T':
      case 'r':
      case 'R': {
        const auto message = string_to_can_message(p_command);
        if (message) {
          transmit_queue.push_back(message.value());
          handled = true;
        }
        break;
      }
    }
  }

  if (handled) {
    // SEND CR
    hal::write(p_serial, hal::as_bytes("\r"sv), hal::never_timeout());
  } else {
    // SEND BELL
    hal::write(p_serial, hal::as_bytes("\x07"sv), hal::never_timeout());
  }
}

void print_encoded_can_message(hal::serial& p_serial,
                               const hal::can::message_t& p_message)
{
  const bool standard = p_message.id < (2 << 11);

  if (standard and not p_message.is_remote_request) {
    // A standard 11-bit CAN frame
    // tiiildd...[CR]
    hal::print<16>(p_serial, "t%03X", p_message.id);
  } else if (not standard and not p_message.is_remote_request) {
    // A extended 29-bit CAN frame
    // Tiiiiiiiildd...[CR]
    hal::print<16>(p_serial, "T%08X", p_message.id);
  } else if (standard and p_message.is_remote_request) {
    // A standard 11-bit CAN frame
    // riii[CR]
    hal::print<16>(p_serial, "r%03X", p_message.id);
  } else if (not standard and p_message.is_remote_request) {
    // A extended 29-bit CAN frame
    // Riiiiiiii[CR]
    hal::print<16>(p_serial, "R%08X", p_message.id);
  }

  // Send data bytes if the message is not a remote request.
  if (not p_message.is_remote_request) {
    hal::print<16>(p_serial, "%X", int(p_message.length));
    for (std::size_t i = 0; i < p_message.length; i++) {
      hal::print<16>(p_serial, "%02X", int(p_message.payload[i]));
    }
  }

  hal::print(p_serial, "\r");
}

void can_receive_handler(const hal::can::message_t& p_message)
{
  if (not receive_queue.full()) {
    receive_queue.push_back(p_message);
  }
}

namespace hal {
/**
 * @ingroup Streams
 * @brief Fill a buffer of bytes until the sequence is found
 *
 */
class stream_fill_upto_v2
{
public:
  /**
   * @ingroup Streams
   * @brief Construct a new fill upto object
   *
   * @param p_sequence - sequence to search for. The lifetime of this data
   * pointed to by this span must outlive this object, or not be used when the
   * lifetime of that data is no longer available.
   * @param p_buffer - buffer to fill data into
   */
  stream_fill_upto_v2(std::span<const hal::byte> p_sequence,
                      std::span<hal::byte> p_buffer);

  friend std::span<const hal::byte> operator|(
    const std::span<const hal::byte>& p_input_data,
    stream_fill_upto_v2& p_self);

  hal::work_state state();
  std::span<hal::byte> span();
  std::span<hal::byte> unfilled();

private:
  std::span<const hal::byte> m_sequence;
  std::span<hal::byte> m_buffer;
  size_t m_fill_amount = 0;
  size_t m_search_index = 0;
};
}  // namespace hal

int main()
{
  using namespace std::literals;
  using namespace hal::literals;

  try {
    hardware_map = initialize_platform();
  } catch (...) {
    hal::halt();
  }

  hal::set_terminate(+[]() { hardware_map.reset(); });

  auto& red_led = *hardware_map.red_led;
  auto& clock = *hardware_map.clock;
  auto& console = *hardware_map.console;
  auto& can = *hardware_map.can;

  can.configure({ .baud_rate = 100.0_kHz });

  // List of commands
  hal::stream_fill_upto_v2 find_end(hal::as_bytes("\r"sv), command_buffer);

  auto reset_command_buffer = [&find_end]() {
    command_buffer.fill(0U);
    find_end = hal::stream_fill_upto_v2(hal::as_bytes("\r"sv), command_buffer);
  };

  can.on_receive(can_receive_handler);

  static std::array<hal::byte, 32> temporary_read_buffer{};
  decltype(console.read(temporary_read_buffer).data) received_console_data{};
  decltype(console.read(temporary_read_buffer).data | find_end) remainder{};

  while (true) {
    if (not remainder.empty()) {
      // Shove remainder back into find end and get the remainder
      // from that. Continue until remainder is empty.
      remainder = remainder | find_end;
    } else {
      received_console_data = console.read(temporary_read_buffer).data;
      remainder = received_console_data | find_end;
    }

    if (hal::finished(find_end)) {
      handle_command(console, can, find_end.span());
      red_led.level(true);
    }

    // hal::stream_fill_upto_v2 terminates
    // Reset the find_end stream if it terminated:
    // - Reached the end of the command buffer without finding a '\r'
    // - Finished finding the '\r' before the end of the
    if (hal::terminated(find_end)) {
      reset_command_buffer();
    }

    if (not transmit_queue.empty()) {
      const auto message = transmit_queue.pop_front();
      can.send(message);
    }

    if (not receive_queue.empty()) {
      const auto message = receive_queue.pop_front();
      print_encoded_can_message(console, message);
    }

    red_led.level(false);
    hal::delay(clock, 1ms);
  }

  return 0;
}

extern "C"
{
  // This gets rid of an issue with libhal-exceptions in Debug mode.
  void __assert_func(const char*, int, const char*, const char*)
  {
    while (true) {
      continue;
    }
  }
}

namespace hal {

stream_fill_upto_v2::stream_fill_upto_v2(std::span<const hal::byte> p_sequence,
                                         std::span<hal::byte> p_buffer)
  : m_sequence(p_sequence)
  , m_buffer(p_buffer)
{
}

std::span<const hal::byte> operator|(
  const std::span<const hal::byte>& p_input_data,
  stream_fill_upto_v2& p_self)
{
  if (p_input_data.empty() ||
      p_self.m_sequence.size() == p_self.m_search_index ||
      p_self.m_buffer.empty()) {
    return p_input_data;
  }

  auto remaining_buffer = p_self.unfilled();
  auto min_size = std::min(p_input_data.size(), remaining_buffer.size());

  for (size_t index = 0; index < min_size; index++) {
    // Check if the search index is equal to the size of the sequence size
    if (p_self.m_search_index == p_self.m_sequence.size()) {
      p_self.m_fill_amount += index;
      return p_input_data.subspan(index);
    }

    // Check if the next byte received matches the sequence
    if (p_self.m_sequence[p_self.m_search_index] == p_input_data[index]) {
      p_self.m_search_index++;
    } else {  // Otherwise set the search index back to the start.
      p_self.m_search_index = 0;
    }

    remaining_buffer[index] = p_input_data[index];
  }

  p_self.m_fill_amount += min_size;
  return p_input_data.subspan(min_size);
}

work_state stream_fill_upto_v2::state()
{
  if (m_search_index == m_sequence.size()) {
    return work_state::finished;
  }
  if (m_fill_amount == m_buffer.size()) {
    return work_state::failed;
  }
  return work_state::in_progress;
}

std::span<hal::byte> stream_fill_upto_v2::span()
{
  return m_buffer.subspan(0, m_fill_amount);
}

std::span<hal::byte> stream_fill_upto_v2::unfilled()
{
  return m_buffer.subspan(m_fill_amount);
}
}  // namespace hal
