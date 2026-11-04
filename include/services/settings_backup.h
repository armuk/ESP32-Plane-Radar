#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

/**
 * Settings backup and restore (portal pages /backup, /backup.json, /restore).
 *
 * The backup is the device's own settings as stored in NVS: location,
 * display and data settings, device name, receiver. It never holds the
 * settings password or the Wi-Fi network (other namespaces, and the
 * password keys are skipped), so a restore keeps the password in use.
 *
 * File format (JSON):
 *   {"plane_radar_settings": 1, "firmware": "v2.1.0",
 *    "settings": {"display": {"textScale": {"i32": 110}, ...}, ...}}
 * Each value names its NVS type: u8, i8, u16, i16, u32, i32, str, or blob
 * (hex). The firmware's own loaders still check every value at the next
 * boot, so a hand-edited file cannot set an out-of-range setting.
 */
namespace services::backup {

constexpr int kFormatVersion = 1;
constexpr size_t kMaxRestoreBytes = 4096;
constexpr size_t kMaxStringLen = 200;
constexpr size_t kMaxBlobLen = 32;

/** NVS namespaces in the backup. */
constexpr const char* kNamespaces[] = {"planeradar", "radar", "display"};

enum class ValueType : uint8_t { kU8, kI8, kU16, kI16, kU32, kI32, kStr, kBlob };

struct Value {
  ValueType type;
  int64_t number;          // integer types
  const char* text;        // kStr: points into the parsed document
  uint8_t blob[kMaxBlobLen];
  size_t blob_len;
};

/** True for a namespace and key a backup may hold (no passwords). */
bool keyAllowed(const char* ns, const char* key);

/** JSON type name for a value type ("u8", "str", ...). */
const char* typeName(ValueType type);

/**
 * Reads one {"<type>": value} entry, checking the type's range and the
 * length limits. On failure sets error to a short reason.
 */
bool parseValue(JsonVariantConst entry, Value& out, const char*& error);

/** Checks the file's marker and format version. */
bool checkHeader(const JsonDocument& doc, const char*& error);

/** Writes the hex form of data (2 * len characters and a NUL) into out. */
void toHex(const uint8_t* data, size_t len, char* out);

#ifdef ARDUINO
/** Fills doc with the current settings. */
void exportSettings(JsonDocument& doc);

/**
 * Writes every allowed value in doc to NVS. Returns how many were written,
 * or -1 with error set when the file is unusable (nothing written then).
 */
int importSettings(const JsonDocument& doc, const char*& error);
#endif

}  // namespace services::backup
