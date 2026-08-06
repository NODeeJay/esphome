#include "coolix_protocol.h"
#include "esphome/core/log.h"

namespace esphome::remote_base {

static const char *const TAG = "remote.coolix";
static const char *const TAG48 = "remote.coolix48";

static const int32_t TICK_US = 560;
static const int32_t HEADER_MARK_US = 8 * TICK_US;
static const int32_t HEADER_SPACE_US = 8 * TICK_US;
static const int32_t BIT_MARK_US = 1 * TICK_US;
static const int32_t BIT_ONE_SPACE_US = 3 * TICK_US;
static const int32_t BIT_ZERO_SPACE_US = 1 * TICK_US;
static const int32_t FOOTER_MARK_US = 1 * TICK_US;
static const int32_t FOOTER_SPACE_US = 10 * TICK_US;
static const uint8_t COOLIX48_BITS = 48;

bool CoolixData::operator==(const CoolixData &other) const {
  if (this->first == 0)
    return this->second == other.first || this->second == other.second;
  if (other.first == 0)
    return other.second == this->first || other.second == this->second;
  return this->first == other.first && this->second == other.second;
}

static void encode_frame(RemoteTransmitData *dst, const uint32_t &src) {
  // Append header
  dst->item(HEADER_MARK_US, HEADER_SPACE_US);
  // Break data into bytes, starting at the Most Significant
  // Byte. Each byte then being sent normal, then followed inverted.
  for (unsigned shift = 16;; shift -= 8) {
    // Grab a bytes worth of data
    const uint8_t byte = src >> shift;
    // Normal
    for (uint8_t mask = 1 << 7; mask; mask >>= 1)
      dst->item(BIT_MARK_US, (byte & mask) ? BIT_ONE_SPACE_US : BIT_ZERO_SPACE_US);
    // Inverted
    for (uint8_t mask = 1 << 7; mask; mask >>= 1)
      dst->item(BIT_MARK_US, (byte & mask) ? BIT_ZERO_SPACE_US : BIT_ONE_SPACE_US);
    // End of frame
    if (shift == 0) {
      // Append footer
      dst->mark(FOOTER_MARK_US);
      break;
    }
  }
}

void CoolixProtocol::encode(RemoteTransmitData *dst, const CoolixData &data) {
  dst->set_carrier_frequency(38000);
  dst->reserve(100 + 100 * data.has_second());
  encode_frame(dst, data.first);
  if (data.has_second()) {
    dst->space(FOOTER_SPACE_US);
    encode_frame(dst, data.second);
  }
}

static bool decode_frame(RemoteReceiveData &src, uint32_t &dst) {
  // Checking for header
  if (!src.expect_item(HEADER_MARK_US, HEADER_SPACE_US))
    return false;
  // Reading data
  uint32_t data = 0;
  for (unsigned n = 3;; data <<= 8) {
    // Reading byte
    for (uint32_t mask = 1 << 7; mask; mask >>= 1) {
      if (!src.expect_mark(BIT_MARK_US))
        return false;
      if (src.expect_space(BIT_ONE_SPACE_US)) {
        data |= mask;
      } else if (!src.expect_space(BIT_ZERO_SPACE_US)) {
        return false;
      }
    }
    // Checking for inverted byte
    for (uint32_t mask = 1 << 7; mask; mask >>= 1) {
      if (!src.expect_item(BIT_MARK_US, (data & mask) ? BIT_ZERO_SPACE_US : BIT_ONE_SPACE_US))
        return false;
    }
    // End of frame
    if (--n == 0) {
      // Checking for footer
      if (!src.expect_mark(FOOTER_MARK_US))
        return false;
      dst = data;
      return true;
    }
  }
}

optional<CoolixData> CoolixProtocol::decode(RemoteReceiveData data) {
  CoolixData result;
  const auto size = data.size();
  if ((size != 200 && size != 100) || !decode_frame(data, result.first))
    return {};
  if (size == 100 || !data.expect_space(FOOTER_SPACE_US) || !decode_frame(data, result.second))
    result.second = 0;
  return result;
}

void CoolixProtocol::dump(const CoolixData &data) {
  if (data.is_strict()) {
    ESP_LOGI(TAG, "Received Coolix: 0x%06" PRIX32, data.first);
  } else if (data.has_second()) {
    ESP_LOGI(TAG, "Received unstrict Coolix: [0x%06" PRIX32 ", 0x%06" PRIX32 "]", data.first, data.second);
  } else {
    ESP_LOGI(TAG, "Received unstrict Coolix: [0x%06" PRIX32 "]", data.first);
  }
}

static void encode_coolix48_frame(RemoteTransmitData *dst, uint64_t data) {
  dst->item(HEADER_MARK_US, HEADER_SPACE_US);
  for (uint64_t mask = 1ULL << (COOLIX48_BITS - 1); mask != 0; mask >>= 1)
    dst->item(BIT_MARK_US, (data & mask) ? BIT_ONE_SPACE_US : BIT_ZERO_SPACE_US);
  dst->mark(FOOTER_MARK_US);
}

void Coolix48Protocol::encode(RemoteTransmitData *dst, const Coolix48Data &data) {
  dst->set_carrier_frequency(38000);
  dst->reserve(200);
  // Coolix remotes send each command twice by default.
  encode_coolix48_frame(dst, data.data);
  dst->space(FOOTER_SPACE_US);
  encode_coolix48_frame(dst, data.data);
}

static bool decode_coolix48_frame(RemoteReceiveData &src, uint64_t &dst) {
  if (!src.expect_item(HEADER_MARK_US, HEADER_SPACE_US))
    return false;

  uint64_t data = 0;
  for (uint8_t bit = 0; bit < COOLIX48_BITS; bit++) {
    data <<= 1;
    if (!src.expect_mark(BIT_MARK_US))
      return false;
    if (src.expect_space(BIT_ONE_SPACE_US)) {
      data |= 1;
    } else if (!src.expect_space(BIT_ZERO_SPACE_US)) {
      return false;
    }
  }
  if (!src.expect_mark(FOOTER_MARK_US))
    return false;
  dst = data;
  return true;
}

static bool is_complemented_coolix24(uint64_t data) {
  // Leave standard Coolix frames to CoolixProtocol so dump: all does not report them twice.
  return (((data >> 40) ^ (data >> 32)) & 0xFF) == 0xFF && (((data >> 24) ^ (data >> 16)) & 0xFF) == 0xFF &&
         (((data >> 8) ^ data) & 0xFF) == 0xFF;
}

optional<Coolix48Data> Coolix48Protocol::decode(RemoteReceiveData data) {
  const auto size = data.size();
  if (size != 100 && size != 200)
    return {};

  Coolix48Data result{};
  if (!decode_coolix48_frame(data, result.data) || is_complemented_coolix24(result.data))
    return {};

  if (size == 200) {
    uint64_t repeated_data;
    if (!data.expect_space(FOOTER_SPACE_US) || !decode_coolix48_frame(data, repeated_data) ||
        repeated_data != result.data)
      return {};
  }
  return result;
}

void Coolix48Protocol::dump(const Coolix48Data &data) {
  ESP_LOGI(TAG48, "Received Coolix48: 0x%012" PRIX64, data.data);
}

}  // namespace esphome::remote_base
