// Minimal JSON helpers, enough for flat config objects and catalog output.
// Keeps the engine dependency-free; not a general-purpose JSON library.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace btb::json {

// A parsed flat object: {"key": number | "string" | true | false, ...}.
// Nested values are rejected. Numbers must be integers.
class FlatObject {
 public:
  static std::optional<FlatObject> parse(std::string_view text, std::string* error = nullptr);

  std::optional<std::int64_t> integer(const std::string& key) const;
  std::optional<std::string> string(const std::string& key) const;
  std::optional<bool> boolean(const std::string& key) const;

 private:
  struct Value {
    enum Kind { Int, Str, Bool } kind;
    std::int64_t i = 0;
    std::string s;
    bool b = false;
  };
  std::map<std::string, Value> values_;
};

// Appends `s` to `out` as a quoted, escaped JSON string.
void append_string(std::string& out, std::string_view s);

}  // namespace btb::json
