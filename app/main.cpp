#include <array>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <span>
#include <string_view>

#include <libhal-exceptions/control.hpp>
#include <libhal-util/as_bytes.hpp>
#include <libhal-util/bit.hpp>
#include <libhal-util/serial.hpp>
#include <libhal-util/steady_clock.hpp>
#include <libhal-util/streams.hpp>
#include <libhal-util/timeout.hpp>
#include <libhal/can.hpp>
#include <libhal/error.hpp>
#include <libhal/steady_clock.hpp>
#include <libhal/timeout.hpp>
#include <libhal/units.hpp>
#include <nonstd/ring_span.hpp>

#include <app/resource_list.hpp>

resource_list hardware_map{};
std::array<hal::byte, 32> command_buffer{};
std::array<hal::can_message, 32> receive_buffer{};
std::array<hal::can_message, 32> transmit_buffer{};
nonstd::ring_span<hal::can_message> receive_queue(receive_buffer.begin(),
                                                  receive_buffer.end());
nonstd::ring_span<hal::can_message> transmit_queue(transmit_buffer.begin(),
                                                   transmit_buffer.end());
bool open;
hal::can_extended_mask_filter::pair global_filter{ .id = 0, .mask = 0 };

constexpr std::string_view version = "V0000";
constexpr std::string_view serial_number = "N0000";

std::span<char const> to_chars(std::span<hal::byte const> p_data)
{
  return { reinterpret_cast<char const*>(p_data.data()), p_data.size() };
}

std::optional<hal::u32> ascii_hex_bytes_to_u32(
  std::span<hal::byte const> p_data)
{
  auto const char_data = to_chars(p_data);
  hal::u32 value = 0;
  auto const status = std::from_chars(
    char_data.data(), char_data.data() + char_data.size(), value, 16);

  if (status.ec != std::errc{}) {
    return std::nullopt;
  }

  return value;
}

bool setup_command(hal::can_bus_manager& p_can,
                   std::span<hal::byte const> p_command)
{
  using namespace hal::literals;

  if (p_command.size() != 3 or open) {
    return false;
  }

  switch (p_command[1]) {
    case '0': {
      p_can.baud_rate(10_kHz);
      break;
    }
    case '1': {
      p_can.baud_rate(20_kHz);
      break;
    }
    case '2': {
      p_can.baud_rate(50_kHz);
      break;
    }
    case '3': {
      p_can.baud_rate(100_kHz);
      break;
    }
    case '4': {
      p_can.baud_rate(125_kHz);
      break;
    }
    case '5': {
      p_can.baud_rate(250_kHz);
      break;
    }
    case '6': {
      p_can.baud_rate(500_kHz);
      break;
    }
    case '7': {
      p_can.baud_rate(800_kHz);
      break;
    }
    case '8': {
      p_can.baud_rate(1_MHz);
      break;
    }
    default: {
      return false;
    }
  }
  return true;
}

// TODO(#14): This function needs to be tested
bool set_custom_baud_rate(hal::can_bus_manager& p_can,
                          std::span<hal::byte const> p_command)
{
  using namespace hal::literals;

  constexpr std::string_view format = "sxxyy\r";
  if (open or p_command.size() != format.size()) {
    return false;
  }

  // CANUSB assumes a SJA0001 device operating at a 16MHz clock frequency
  constexpr auto oscillator_frequency = 16_MHz;
  constexpr auto baud_rate_prescaler = hal::bit_mask::from(0, 5);
  [[maybe_unused]] constexpr auto synchronization_jump_width =
    hal::bit_mask::from(6, 7);
  constexpr auto time_segment_1 = hal::bit_mask::from(0, 3);
  constexpr auto time_segment_2 = hal::bit_mask::from(4, 6);
  [[maybe_unused]] constexpr auto sampling = hal::bit_mask::from(7);

  auto const byte1 = ascii_hex_bytes_to_u32(p_command.subspan(1, 1));
  auto const byte2 = ascii_hex_bytes_to_u32(p_command.subspan(2, 1));

  if (not byte1 or not byte2) {
    return false;
  }

  auto const baud_rate = hal::bit_extract<baud_rate_prescaler>(*byte1);
  auto const tseg1 = hal::bit_extract<time_segment_1>(*byte2);
  auto const tseg2 = hal::bit_extract<time_segment_2>(*byte2);

  // Equation:
  //
  //     Bit Rate = Fosc / (2 * BRP * (1 + TSEG1 + TSEG2))
  //
  auto const bit_rate =
    oscillator_frequency / ((2 * baud_rate) * (1 + tseg1 + tseg2));

  try {
    p_can.baud_rate(bit_rate);
  } catch (hal::operation_not_supported const&) {
    // Failed to set the baud rate return failure
    return false;
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

  // Bit 0 receive queue full
  if (receive_queue.full()) {
    status |= 1 << 0;
  }

  // Bit 1 transmit queue full
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

std::optional<hal::can_message> string_to_can_message(
  std::span<hal::byte const> p_command)
{
  hal::can_message message{};
  std::size_t format_size = 0;
  std::size_t id_byte_length = 0;
  auto const command = p_command[0];
  auto command_chars = to_chars(p_command);

  if (command == 'r' or command == 't') {
    constexpr std::string_view format = "tiiil\r";
    format_size = format.size();
    id_byte_length = 3;
    message.extended(false);
  } else if (command == 'R' or command == 'T') {
    constexpr std::string_view format = "Tiiiiiiiil\r";
    format_size = format.size();
    id_byte_length = 8;
    message.extended(true);
  }

  if (command_chars.size() < format_size) {
    return std::nullopt;
  }

  if (command == 'r' or command == 'R') {
    message.remote_request(true);
  } else {
    message.remote_request(false);
  }

  // Skip first character
  command_chars = command_chars.subspan(1);

  // Scope for status variable
  {
    hal::u32 id = 0;
    auto const status = std::from_chars(
      &command_chars[0], &command_chars[id_byte_length], id, 16);

    if (status.ec != std::errc{}) {
      return std::nullopt;
    }
    message.id(id);
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

  if (not valid_payload_length or not length_within_bounds) {
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

bool sets_acceptance_code_register(hal::can_extended_mask_filter& p_filter,
                                   std::span<hal::byte const> p_command)
{
  constexpr std::string_view format = "Mxxxxxxxx\r";
  // -1 to remove the null character length
  if (open or p_command.size() < (format.size() - 1)) {
    return false;
  }

  // subspan 1 to ignore the command character
  auto const register_value = ascii_hex_bytes_to_u32(p_command.subspan(1));

  if (not register_value) {
    return false;
  }

  if (p_command[0] == 'M') {
    global_filter.id = *register_value;
  } else if (p_command[0] == 'm') {
    global_filter.mask = *register_value;
  } else {
    return false;
  }

  p_filter.allow(global_filter);

  return true;
}

void handle_command(hal::serial& p_serial,
                    hal::can_bus_manager& p_can_manager,
                    hal::can_extended_mask_filter& p_filter,
                    std::span<hal::byte const> p_command)
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
    switch (p_command[0]) {
      case 'S': {
        handled = setup_command(p_can_manager, p_command);
        break;
      }
      case 's': {
        // TODO(#14): This needs to be tested
        handled = set_custom_baud_rate(p_can_manager, p_command);
        break;
      }
      case 'O': {
        handled = open_command();
        break;
      }
      case 'M':
      case 'm': {
        handled = sets_acceptance_code_register(p_filter, p_command);
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
      case 'r':
      case 'T':
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
                               const hal::can_message& p_message)
{
  const bool standard = not p_message.extended();

  if (standard and not p_message.remote_request()) {
    // A standard 11-bit CAN frame
    // tiiildd...[CR]
    hal::print<16>(p_serial, "t%03X", p_message.id());
  } else if (not standard and not p_message.remote_request()) {
    // A extended 29-bit CAN frame
    // Tiiiiiiiildd...[CR]
    hal::print<16>(p_serial, "T%08X", p_message.id());
  } else if (standard and p_message.remote_request()) {
    // A standard 11-bit CAN frame
    // riii[CR]
    hal::print<16>(p_serial, "r%03X", p_message.id());
  } else if (not standard and p_message.remote_request()) {
    // A extended 29-bit CAN frame
    // Riiiiiiii[CR]
    hal::print<16>(p_serial, "R%08X", p_message.id());
  }

  // Send data bytes if the message is not a remote request.
  if (not p_message.remote_request()) {
    hal::print<16>(p_serial, "%X", int(p_message.length));
    for (std::size_t i = 0; i < p_message.length; i++) {
      hal::print<16>(p_serial, "%02X", int(p_message.payload[i]));
    }
  }

  hal::print(p_serial, "\r");
}

void can_receive_handler(hal::can_interrupt::on_receive_tag,
                         const hal::can_message& p_message)
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
  stream_fill_upto_v2(std::span<hal::byte const> p_sequence,
                      std::span<hal::byte> p_buffer);

  friend std::span<const hal::byte> operator|(
    const std::span<const hal::byte>& p_input_data,
    stream_fill_upto_v2& p_self);

  hal::work_state state();
  std::span<hal::byte const> span();
  std::span<hal::byte> unfilled();

private:
  std::span<hal::byte const> m_sequence;
  std::span<hal::byte> m_buffer;
  size_t m_fill_amount = 0;
  size_t m_search_index = 0;
};
}  // namespace hal

int main()
{
  using namespace std::literals;
  using namespace hal::literals;

  hal::set_terminate(+[]() {
    if (hardware_map.reset) {
      (*hardware_map.reset)();
    }

    while (true) {
      // Wait for debugger
      continue;
    }
  });

  try {
    initialize_platform(hardware_map);
  } catch (...) {
    hal::halt();
  }

  auto& red_led = *hardware_map.red_led.value();
  auto& clock = *hardware_map.clock.value();
  auto& console = *hardware_map.console.value();
  auto& can = *hardware_map.can_transceiver.value();
  auto& can_interrupt = *hardware_map.can_interrupt.value();
  auto& can_bus_manager = *hardware_map.can_bus_manager.value();
  auto& can_mask_filter = *hardware_map.can_mask_filter.value();

  can_bus_manager.baud_rate(100_kHz);
  can_mask_filter.allow(global_filter);

  // List of commands
  hal::stream_fill_upto_v2 find_end(hal::as_bytes("\r"sv), command_buffer);

  auto reset_command_buffer = [&find_end]() {
    command_buffer.fill(0U);
    find_end = hal::stream_fill_upto_v2(hal::as_bytes("\r"sv), command_buffer);
  };

  can_interrupt.on_receive(can_receive_handler);

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
      handle_command(
        console, can_bus_manager, can_mask_filter, find_end.span());
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

stream_fill_upto_v2::stream_fill_upto_v2(std::span<hal::byte const> p_sequence,
                                         std::span<hal::byte> p_buffer)
  : m_sequence(p_sequence)
  , m_buffer(p_buffer)
{
}

std::span<const hal::byte> operator|(
  const std::span<const hal::byte>& p_input_data,
  stream_fill_upto_v2& p_self)
{
  if (p_input_data.empty() or
      p_self.m_sequence.size() == p_self.m_search_index or
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

std::span<hal::byte const> stream_fill_upto_v2::span()
{
  return m_buffer.subspan(0, m_fill_amount);
}

std::span<hal::byte> stream_fill_upto_v2::unfilled()
{
  return m_buffer.subspan(m_fill_amount);
}
}  // namespace hal
