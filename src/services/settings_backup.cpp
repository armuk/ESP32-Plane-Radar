#include "services/settings_backup.h"

#include <cstring>
#include <iterator>

#ifdef ARDUINO
#include <nvs.h>

#include "version.h"
#endif

namespace services::backup {
namespace {

constexpr const char* kSecretKeys[] = {"otaPass", "otaPassNew"};
constexpr const char* kTypeNames[] = {"u8", "i8", "u16", "i16",
                                      "u32", "i32", "str", "blob"};
constexpr size_t kTypeCount = sizeof(kTypeNames) / sizeof(kTypeNames[0]);
constexpr size_t kMaxKeyLen = 15;  // NVS limit

struct Range {
  int64_t min;
  int64_t max;
};
constexpr Range kRanges[] = {
    {0, 0xFF},       {-128, 127},           {0, 0xFFFF},
    {-32768, 32767}, {0, 0xFFFFFFFFLL},     {-2147483648LL, 2147483647LL},
};

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

bool keyAllowed(const char* ns, const char* key) {
  if (ns == nullptr || key == nullptr) {
    return false;
  }
  const size_t key_len = strlen(key);
  if (key_len == 0 || key_len > kMaxKeyLen) {
    return false;
  }
  bool ns_ok = false;
  for (const char* allowed : kNamespaces) {
    ns_ok = ns_ok || strcmp(ns, allowed) == 0;
  }
  if (!ns_ok) {
    return false;
  }
  for (const char* secret : kSecretKeys) {
    if (strcmp(key, secret) == 0) {
      return false;
    }
  }
  return true;
}

const char* typeName(ValueType type) {
  const size_t i = static_cast<size_t>(type);
  return i < kTypeCount ? kTypeNames[i] : "?";
}

bool parseValue(JsonVariantConst entry, Value& out, const char*& error) {
  JsonObjectConst obj = entry.as<JsonObjectConst>();
  if (obj.isNull() || obj.size() != 1) {
    error = "each value must be {\"type\": value}";
    return false;
  }
  JsonPairConst pair = *obj.begin();
  size_t type_index = kTypeCount;
  for (size_t i = 0; i < kTypeCount; ++i) {
    if (strcmp(pair.key().c_str(), kTypeNames[i]) == 0) {
      type_index = i;
    }
  }
  if (type_index == kTypeCount) {
    error = "unknown value type";
    return false;
  }
  out = Value{};
  out.type = static_cast<ValueType>(type_index);
  JsonVariantConst v = pair.value();

  if (out.type == ValueType::kStr) {
    if (!v.is<const char*>()) {
      error = "str value is not text";
      return false;
    }
    out.text = v.as<const char*>();
    if (strlen(out.text) > kMaxStringLen) {
      error = "text value too long";
      return false;
    }
    return true;
  }
  if (out.type == ValueType::kBlob) {
    if (!v.is<const char*>()) {
      error = "blob value is not hex text";
      return false;
    }
    const char* hex = v.as<const char*>();
    const size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0 || hex_len / 2 > kMaxBlobLen) {
      error = "blob value has a bad length";
      return false;
    }
    for (size_t i = 0; i < hex_len / 2; ++i) {
      const int hi = hexDigit(hex[2 * i]);
      const int lo = hexDigit(hex[2 * i + 1]);
      if (hi < 0 || lo < 0) {
        error = "blob value is not hex";
        return false;
      }
      out.blob[i] = static_cast<uint8_t>(hi * 16 + lo);
    }
    out.blob_len = hex_len / 2;
    return true;
  }
  // Integers: whole numbers only, inside the type's range.
  if (!v.is<int64_t>()) {
    error = "number value is not a whole number";
    return false;
  }
  const int64_t n = v.as<int64_t>();
  const Range& r = kRanges[type_index];
  if (n < r.min || n > r.max) {
    error = "number value out of range";
    return false;
  }
  out.number = n;
  return true;
}

bool checkHeader(const JsonDocument& doc, const char*& error) {
  if (doc["plane_radar_settings"].as<int>() != kFormatVersion) {
    error = "this is not a Plane Radar settings backup (or a newer format)";
    return false;
  }
  if (!doc["settings"].is<JsonObjectConst>()) {
    error = "the backup has no settings";
    return false;
  }
  return true;
}

void toHex(const uint8_t* data, size_t len, char* out) {
  static const char kDigits[] = "0123456789abcdef";
  for (size_t i = 0; i < len; ++i) {
    out[2 * i] = kDigits[data[i] >> 4];
    out[2 * i + 1] = kDigits[data[i] & 0x0F];
  }
  out[2 * len] = '\0';
}

#ifdef ARDUINO
namespace {

/** Adds one NVS entry to obj; skips types the firmware never stores. */
void exportEntry(nvs_handle_t h, const nvs_entry_info_t& info,
                 JsonObject obj) {
  switch (info.type) {
#define PR_EXPORT_INT(nvs_type, c_type, getter, name) \
  case nvs_type: {                                    \
    c_type v = 0;                                     \
    if (getter(h, info.key, &v) == ESP_OK) {          \
      obj[info.key].to<JsonObject>()[name] = v;       \
    }                                                 \
    return;                                           \
  }
    PR_EXPORT_INT(NVS_TYPE_U8, uint8_t, nvs_get_u8, "u8")
    PR_EXPORT_INT(NVS_TYPE_I8, int8_t, nvs_get_i8, "i8")
    PR_EXPORT_INT(NVS_TYPE_U16, uint16_t, nvs_get_u16, "u16")
    PR_EXPORT_INT(NVS_TYPE_I16, int16_t, nvs_get_i16, "i16")
    PR_EXPORT_INT(NVS_TYPE_U32, uint32_t, nvs_get_u32, "u32")
    PR_EXPORT_INT(NVS_TYPE_I32, int32_t, nvs_get_i32, "i32")
#undef PR_EXPORT_INT
    case NVS_TYPE_STR: {
      char text[kMaxStringLen + 1];
      size_t len = sizeof(text);
      if (nvs_get_str(h, info.key, text, &len) == ESP_OK) {
        obj[info.key].to<JsonObject>()["str"] = text;
      }
      return;
    }
    case NVS_TYPE_BLOB: {
      uint8_t data[kMaxBlobLen];
      size_t len = sizeof(data);
      if (nvs_get_blob(h, info.key, data, &len) == ESP_OK) {
        char hex[2 * kMaxBlobLen + 1];
        toHex(data, len, hex);
        obj[info.key].to<JsonObject>()["blob"] = hex;
      }
      return;
    }
    default:
      return;
  }
}

esp_err_t writeValue(nvs_handle_t h, const char* key, const Value& v) {
  switch (v.type) {
    case ValueType::kU8: return nvs_set_u8(h, key, static_cast<uint8_t>(v.number));
    case ValueType::kI8: return nvs_set_i8(h, key, static_cast<int8_t>(v.number));
    case ValueType::kU16: return nvs_set_u16(h, key, static_cast<uint16_t>(v.number));
    case ValueType::kI16: return nvs_set_i16(h, key, static_cast<int16_t>(v.number));
    case ValueType::kU32: return nvs_set_u32(h, key, static_cast<uint32_t>(v.number));
    case ValueType::kI32: return nvs_set_i32(h, key, static_cast<int32_t>(v.number));
    case ValueType::kStr: return nvs_set_str(h, key, v.text);
    case ValueType::kBlob: return nvs_set_blob(h, key, v.blob, v.blob_len);
  }
  return ESP_ERR_INVALID_ARG;
}

}  // namespace

void exportSettings(JsonDocument& doc) {
  doc["plane_radar_settings"] = kFormatVersion;
  doc["firmware"] = config::kFirmwareVersion;
  JsonObject settings = doc["settings"].to<JsonObject>();
  for (const char* ns : kNamespaces) {
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) {
      continue;  // never written: nothing to back up
    }
    JsonObject obj = settings[ns].to<JsonObject>();
    nvs_iterator_t it = nullptr;
    esp_err_t res = nvs_entry_find(NVS_DEFAULT_PART_NAME, ns, NVS_TYPE_ANY, &it);
    while (res == ESP_OK) {
      nvs_entry_info_t info;
      nvs_entry_info(it, &info);
      if (keyAllowed(ns, info.key)) {
        exportEntry(h, info, obj);
      }
      res = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    nvs_close(h);
  }
}

int importSettings(const JsonDocument& doc, const char*& error) {
  if (!checkHeader(doc, error)) {
    return -1;
  }
  JsonObjectConst settings = doc["settings"].as<JsonObjectConst>();

  // Check everything first, so a bad file changes nothing.
  for (JsonPairConst ns : settings) {
    JsonObjectConst entries = ns.value().as<JsonObjectConst>();
    if (entries.isNull()) {
      error = "a settings group is not an object";
      return -1;
    }
    for (JsonPairConst kv : entries) {
      Value v;
      if (!parseValue(kv.value(), v, error)) {
        return -1;
      }
    }
  }

  int written = 0;
  for (const char* ns : kNamespaces) {
    JsonObjectConst entries = settings[ns].as<JsonObjectConst>();
    if (entries.isNull()) {
      continue;
    }
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READWRITE, &h) != ESP_OK) {
      continue;
    }
    for (JsonPairConst kv : entries) {
      Value v;
      if (keyAllowed(ns, kv.key().c_str()) &&
          parseValue(kv.value(), v, error) &&
          writeValue(h, kv.key().c_str(), v) == ESP_OK) {
        ++written;
      }
    }
    nvs_commit(h);
    nvs_close(h);
  }
  error = nullptr;
  return written;
}
#endif

}  // namespace services::backup
