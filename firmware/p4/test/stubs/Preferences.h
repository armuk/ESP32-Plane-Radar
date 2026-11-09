#pragma once

/** In-memory Preferences (NVS) stand-in for the native unit tests. */

#include <Arduino.h>

#include <map>
#include <string>

class Preferences {
 public:
  bool begin(const char*, bool) { return true; }
  void end() {}
  void clear() { store().clear(); }

  void putBool(const char* key, bool v) { store()[key] = v ? "1" : "0"; }
  void putInt(const char* key, int v) { store()[key] = std::to_string(v); }
  void putUChar(const char* key, uint8_t v) { putInt(key, v); }
  void putString(const char* key, const char* v) { store()[key] = v; }
  void putDouble(const char* key, double v) { store()[key] = std::to_string(v); }

  bool getBool(const char* key, bool d) {
    auto it = store().find(key);
    return it == store().end() ? d : it->second == "1";
  }
  int getInt(const char* key, int d) {
    auto it = store().find(key);
    return it == store().end() ? d : std::stoi(it->second);
  }
  uint8_t getUChar(const char* key, uint8_t d) { return getInt(key, d); }
  double getDouble(const char* key, double d) {
    auto it = store().find(key);
    return it == store().end() ? d : std::stod(it->second);
  }
  String getString(const char* key, const char* d) {
    auto it = store().find(key);
    return it == store().end() ? String(d) : String(it->second.c_str());
  }
  bool isKey(const char* key) { return store().count(key) != 0; }
  bool remove(const char* key) { return store().erase(key) != 0; }

  /** Shared by every instance, like one NVS partition. */
  static std::map<std::string, std::string>& store() {
    static std::map<std::string, std::string> s;
    return s;
  }
};
