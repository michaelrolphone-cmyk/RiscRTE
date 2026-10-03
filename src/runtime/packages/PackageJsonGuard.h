#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace RuntimePackages {

// A lexical gate for *untrusted metadata*, before ArduinoJson (which normally
// retains only one value when an object contains duplicate keys). Not an
// envelope decoder, canonicalizer, signature verifier or authorization check.
// A bounded, immutable source is required; no heap, ELF load or storage I/O.
// Object keys must be unescaped printable ASCII. This prevents escaped-key
// aliases such as "id" and "\u0069d" from bypassing duplicate detection.
class PackageJsonGuard {
 public:
  static constexpr size_t kMaxBytes = 65536;
  static constexpr size_t kMaxDepth = 10;
  static constexpr size_t kMaxActiveKeys = 64;
  static constexpr size_t kMaxKeyBytes = 63;
  static constexpr size_t kMaxArrayItems = 128;

  PackageJsonGuard(const char* data, size_t length) : data_(data), length_(length) {}

  bool objectOnly() {
    if (!data_ || !length_ || length_ > kMaxBytes) return false;
    skipWhitespace();
    if (!object()) return false;
    skipWhitespace();
    return position_ == length_;
  }

 private:
  struct Key { size_t offset; size_t length; };
  const char* data_;
  size_t length_;
  size_t position_ = 0;
  size_t activeKeys_ = 0;
  size_t depth_ = 0;
  Key keys_[kMaxActiveKeys]{};

  void skipWhitespace() {
    while (position_ < length_) {
      const char c = data_[position_];
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
      ++position_;
    }
  }

  bool take(char expected) {
    if (position_ >= length_ || data_[position_] != expected) return false;
    ++position_;
    return true;
  }

  static int hex(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  bool unicode(uint16_t* value) {
    if (!value || position_ + 4 > length_) return false;
    uint16_t result = 0;
    for (size_t i = 0; i < 4; ++i) {
      const int digit = hex(static_cast<unsigned char>(data_[position_++]));
      if (digit < 0) return false;
      result = static_cast<uint16_t>((result << 4) | digit);
    }
    *value = result;
    return true;
  }

  bool string(bool key, size_t* offset = nullptr, size_t* length = nullptr) {
    if (!take('"')) return false;
    const size_t start = position_;
    while (position_ < length_) {
      const unsigned char c = static_cast<unsigned char>(data_[position_++]);
      if (c == '"') {
        if (key) {
          const size_t count = position_ - start - 1;
          if (!count || count > kMaxKeyBytes) return false;
          if (offset) *offset = start;
          if (length) *length = count;
        }
        return true;
      }
      if (c < 0x20 || (key && c >= 0x7f)) return false;
      if (c != '\\') continue;
      if (key || position_ == length_) return false;
      const char escaped = data_[position_++];
      if (escaped == '"' || escaped == '\\' || escaped == '/' || escaped == 'b' ||
          escaped == 'f' || escaped == 'n' || escaped == 'r' || escaped == 't') continue;
      if (escaped != 'u') return false;
      uint16_t first = 0;
      if (!unicode(&first)) return false;
      if (first >= 0xdc00 && first <= 0xdfff) return false;
      if (first >= 0xd800 && first <= 0xdbff) {
        if (!take('\\') || !take('u')) return false;
        uint16_t second = 0;
        if (!unicode(&second) || second < 0xdc00 || second > 0xdfff) return false;
      }
    }
    return false;
  }

  bool literal(const char* word) {
    const size_t size = std::strlen(word);
    if (size > length_ - position_ || std::memcmp(data_ + position_, word, size)) return false;
    position_ += size;
    return true;
  }

  bool digits() {
    const size_t start = position_;
    while (position_ < length_ && data_[position_] >= '0' && data_[position_] <= '9')
      ++position_;
    return position_ != start;
  }

  bool number() {
    (void)take('-');
    if (take('0')) {
      // A following digit is rejected by the enclosing delimiter check.
    } else {
      if (position_ >= length_ || data_[position_] < '1' || data_[position_] > '9') return false;
      if (!digits()) return false;
    }
    if (take('.') && !digits()) return false;
    if (position_ < length_ && (data_[position_] == 'e' || data_[position_] == 'E')) {
      ++position_;
      if (position_ < length_ && (data_[position_] == '+' || data_[position_] == '-')) ++position_;
      if (!digits()) return false;
    }
    return true;
  }

  bool value() {
    skipWhitespace();
    if (position_ >= length_) return false;
    switch (data_[position_]) {
      case '{': return object();
      case '[': return array();
      case '"': return string(false);
      case 't': return literal("true");
      case 'f': return literal("false");
      case 'n': return literal("null");
      default: return number();
    }
  }

  bool object() {
    if (depth_ >= kMaxDepth || !take('{')) return false;
    ++depth_;
    const size_t firstKey = activeKeys_;
    skipWhitespace();
    if (take('}')) { --depth_; return true; }
    for (;;) {
      size_t start = 0, size = 0;
      if (!string(true, &start, &size)) return false;
      for (size_t i = firstKey; i < activeKeys_; ++i) {
        if (keys_[i].length == size && !std::memcmp(data_ + keys_[i].offset, data_ + start, size))
          return false;
      }
      if (activeKeys_ >= kMaxActiveKeys) return false;
      keys_[activeKeys_++] = {start, size};
      skipWhitespace();
      if (!take(':') || !value()) return false;
      skipWhitespace();
      if (take('}')) {
        activeKeys_ = firstKey;
        --depth_;
        return true;
      }
      if (!take(',')) return false;
      skipWhitespace();
    }
  }

  bool array() {
    if (depth_ >= kMaxDepth || !take('[')) return false;
    ++depth_;
    skipWhitespace();
    if (take(']')) { --depth_; return true; }
    size_t count = 0;
    for (;;) {
      if (++count > kMaxArrayItems || !value()) return false;
      skipWhitespace();
      if (take(']')) { --depth_; return true; }
      if (!take(',')) return false;
    }
  }
};

inline bool safePackageJsonObject(const char* data, size_t bytes) {
  return PackageJsonGuard(data, bytes).objectOnly();
}

}  // namespace RuntimePackages
