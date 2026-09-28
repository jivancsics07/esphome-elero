#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "device_type.h"
#include "elero_packet.h"

namespace esphome {
namespace elero {

/// NVS config version — bump when struct layout changes
/// (v3: added updated_at, v4: device name grown from 24 to 48 bytes)
constexpr uint8_t NVS_CONFIG_VERSION = 4;

/// Layout version of NvsDeviceConfigV3, migrated on restore.
constexpr uint8_t NVS_CONFIG_VERSION_V3 = 3;

/// NVS group config version — separate compound objects, not device slots.
constexpr uint8_t NVS_GROUP_CONFIG_VERSION = 1;

/// Maximum group name length (including null terminator)
constexpr size_t NVS_NAME_MAX = 24;

/// Maximum device name length in bytes (including null terminator).
/// Names are UTF-8, so every umlaut costs two bytes.
constexpr size_t NVS_DEVICE_NAME_MAX = 48;

/// Copy a UTF-8 string into a fixed buffer without splitting a multi-byte
/// sequence: if the string does not fit, it is cut at the last full character.
inline void copy_utf8_truncated(char *dst, size_t dst_size, const char *src) {
  if (dst_size == 0) return;
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }
  size_t len = strnlen(src, dst_size);
  if (len >= dst_size) {
    len = dst_size - 1;
    // Back off continuation bytes (10xxxxxx) and the lead byte they belong to.
    while (len > 0 && (static_cast<uint8_t>(src[len]) & 0xC0) == 0x80) {
      --len;
    }
  }
  memcpy(dst, src, len);
  dst[len] = '\0';
}

/// Maximum group id length (including null terminator)
constexpr size_t NVS_GROUP_ID_MAX = 24;

/// Maximum group members. Device ids are stable hardware destination addresses.
constexpr size_t NVS_GROUP_MAX_MEMBERS = 48;

/// NVS preference hash keys — must be unique per manager type to avoid collisions.
/// MQTT mode uses "elero_cover", NVS mode uses "elero_nvs_cover", etc.
namespace nvs_pref_key {
inline constexpr const char *COVER = "elero_cover";
inline constexpr const char *LIGHT = "elero_light";
inline constexpr const char *REMOTE = "elero_remote";
inline constexpr const char *NVS_COVER = "elero_nvs_cover";
inline constexpr const char *NVS_LIGHT = "elero_nvs_light";
inline constexpr const char *NVS_REMOTE = "elero_nvs_remote";
inline constexpr const char *GROUP = "elero_group";
}  // namespace nvs_pref_key

/// Fixed-size device configuration persisted via ESPHome preferences.
/// Each pre-allocated slot stores its own config independently.
/// Layout is stable — bump NVS_CONFIG_VERSION when changing fields.
struct NvsDeviceConfig {
  // Header (4 bytes)
  uint8_t version{NVS_CONFIG_VERSION};
  DeviceType type{DeviceType::COVER};
  static constexpr uint8_t FLAG_ENABLED = 0x01;
  uint8_t flags{FLAG_ENABLED};  ///< bit 0 = enabled
  uint8_t ha_device_class{0};  ///< HaCoverClass enum value (0 = shutter, backward compatible)

  // RF addressing (16 bytes)
  uint32_t dst_address{0};
  uint32_t src_address{0};
  uint8_t channel{0};
  uint8_t hop{packet::defaults::HOP};
  uint8_t payload_1{packet::defaults::PAYLOAD_1};
  uint8_t payload_2{packet::defaults::PAYLOAD_2};
  uint8_t type_byte{packet::msg_type::COMMAND};
  uint8_t type2{packet::defaults::TYPE2};
  uint8_t supports_tilt{0};  ///< Cover: 1 = tilt supported
  uint8_t rf_reserved{0};

  // Timing (16 bytes)
  uint32_t open_duration_ms{0};
  uint32_t close_duration_ms{0};
  uint32_t poll_interval_ms_reserved{0};  ///< DEPRECATED: kept for NVS struct layout compat, ignored at runtime
  uint32_t dim_duration_ms{0};        ///< Light: 0 = on/off only, >0 = brightness control

  // Metadata (4 bytes)
  uint32_t updated_at{0};  ///< millis() when last persisted (0 = never)

  // Name (48 bytes)
  char name[NVS_DEVICE_NAME_MAX]{};

  // ─── Helpers ───

  bool is_enabled() const { return flags & FLAG_ENABLED; }
  void set_enabled(bool en) { en ? (flags |= FLAG_ENABLED) : (flags &= ~FLAG_ENABLED); }

  bool is_valid() const { return version == NVS_CONFIG_VERSION && dst_address != 0; }
  bool is_cover() const { return type == DeviceType::COVER; }
  bool is_light() const { return type == DeviceType::LIGHT; }
  bool is_remote() const { return type == DeviceType::REMOTE; }

  void set_name(const char *n) { copy_utf8_truncated(name, sizeof(name), n); }
};

static_assert(sizeof(NvsDeviceConfig) == 88, "NvsDeviceConfig must be 88 bytes for NVS storage");

/// v3 device slot layout (24-byte name). Only read, to migrate slots written by
/// older firmware; ESPHome preferences reject a blob whose size does not match,
/// so the v4 struct cannot load these directly.
struct NvsDeviceConfigV3 {
  uint8_t version{0};
  DeviceType type{DeviceType::COVER};
  uint8_t flags{0};
  uint8_t ha_device_class{0};
  uint32_t dst_address{0};
  uint32_t src_address{0};
  uint8_t channel{0};
  uint8_t hop{0};
  uint8_t payload_1{0};
  uint8_t payload_2{0};
  uint8_t type_byte{0};
  uint8_t type2{0};
  uint8_t supports_tilt{0};
  uint8_t rf_reserved{0};
  uint32_t open_duration_ms{0};
  uint32_t close_duration_ms{0};
  uint32_t poll_interval_ms_reserved{0};
  uint32_t dim_duration_ms{0};
  uint32_t updated_at{0};
  char name[24]{};

  bool is_valid() const { return version == NVS_CONFIG_VERSION_V3 && dst_address != 0; }

  NvsDeviceConfig migrate() const {
    NvsDeviceConfig cfg{};
    cfg.type = type;
    cfg.flags = flags;
    cfg.ha_device_class = ha_device_class;
    cfg.dst_address = dst_address;
    cfg.src_address = src_address;
    cfg.channel = channel;
    cfg.hop = hop;
    cfg.payload_1 = payload_1;
    cfg.payload_2 = payload_2;
    cfg.type_byte = type_byte;
    cfg.type2 = type2;
    cfg.supports_tilt = supports_tilt;
    cfg.rf_reserved = rf_reserved;
    cfg.open_duration_ms = open_duration_ms;
    cfg.close_duration_ms = close_duration_ms;
    cfg.poll_interval_ms_reserved = poll_interval_ms_reserved;
    cfg.dim_duration_ms = dim_duration_ms;
    cfg.updated_at = updated_at;
    char buf[sizeof(name) + 1]{};
    memcpy(buf, name, sizeof(name));  // v3 names are always NUL-terminated, but be safe
    cfg.set_name(buf);
    return cfg;
  }
};

static_assert(sizeof(NvsDeviceConfigV3) == 64, "NvsDeviceConfigV3 must match the v3 64-byte layout");

/// Fixed-size group configuration persisted via ESPHome preferences.
/// Groups are pure membership metadata: id + display name + stable device ids.
/// Device type is intentionally not stored; it is derived from referenced devices
/// and guarded during upsert/command validation.
struct NvsGroupConfig {
  uint8_t version{NVS_GROUP_CONFIG_VERSION};
  uint8_t member_count{0};
  uint16_t reserved{0};

  char id[NVS_GROUP_ID_MAX]{};
  char name[NVS_NAME_MAX]{};
  uint32_t device_ids[NVS_GROUP_MAX_MEMBERS]{};

  bool is_valid() const {
    return version == NVS_GROUP_CONFIG_VERSION && id[0] != '\0' &&
           member_count > 0 && member_count <= NVS_GROUP_MAX_MEMBERS;
  }

  void set_id(const char *value) {
    if (value == nullptr) {
      id[0] = '\0';
      return;
    }
    strncpy(id, value, NVS_GROUP_ID_MAX - 1);
    id[NVS_GROUP_ID_MAX - 1] = '\0';
  }

  void set_name(const char *value) { copy_utf8_truncated(name, sizeof(name), value); }
};

static_assert(sizeof(NvsGroupConfig) == 244, "NvsGroupConfig layout changed unexpectedly");

}  // namespace elero
}  // namespace esphome
