#include "btb/json.hpp"

#include <cctype>

namespace btb::json {

namespace {

struct Parser {
  std::string_view t;
  std::size_t i = 0;
  std::string err;

  void ws() {
    while (i < t.size() && std::isspace(static_cast<unsigned char>(t[i]))) ++i;
  }
  bool eat(char c) {
    ws();
    if (i < t.size() && t[i] == c) {
      ++i;
      return true;
    }
    return false;
  }
  bool fail(const char* msg) {
    if (err.empty()) err = std::string(msg) + " at offset " + std::to_string(i);
    return false;
  }
  bool str(std::string& out) {
    if (!eat('"')) return fail("expected string");
    while (i < t.size() && t[i] != '"') {
      char c = t[i++];
      if (c == '\\') {
        if (i >= t.size()) return fail("bad escape");
        const char e = t[i++];
        switch (e) {
          case 'n': c = '\n'; break;
          case 't': c = '\t'; break;
          case '"': case '\\': case '/': c = e; break;
          default: return fail("unsupported escape");
        }
      }
      out.push_back(c);
    }
    if (i >= t.size()) return fail("unterminated string");
    ++i;
    return true;
  }
  bool integer(std::int64_t& out) {
    ws();
    bool neg = false;
    if (i < t.size() && t[i] == '-') {
      neg = true;
      ++i;
    }
    if (i >= t.size() || !std::isdigit(static_cast<unsigned char>(t[i]))) return fail("expected integer");
    std::int64_t v = 0;
    while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) {
      v = v * 10 + (t[i++] - '0');
      if (v > (std::int64_t{1} << 53)) return fail("integer too large");
    }
    if (i < t.size() && (t[i] == '.' || t[i] == 'e' || t[i] == 'E')) return fail("only integers are supported");
    out = neg ? -v : v;
    return true;
  }
  bool literal(std::string_view word) {
    ws();
    if (t.substr(i, word.size()) != word) return false;
    i += word.size();
    return true;
  }
};

}  // namespace

std::optional<FlatObject> FlatObject::parse(std::string_view text, std::string* error) {
  Parser p{text, 0, {}};
  FlatObject obj;
  auto failed = [&]() -> std::optional<FlatObject> {
    if (error) *error = p.err.empty() ? "invalid JSON" : p.err;
    return std::nullopt;
  };
  p.ws();
  if (p.i == text.size()) return obj;  // empty string = no overrides
  if (!p.eat('{')) return p.fail("expected '{'"), failed();
  if (!p.eat('}')) {
    do {
      std::string key;
      if (!p.str(key)) return failed();
      if (!p.eat(':')) return p.fail("expected ':'"), failed();
      p.ws();
      Value v{Value::Int, 0, {}, false};
      if (p.i < text.size() && text[p.i] == '"') {
        v.kind = Value::Str;
        if (!p.str(v.s)) return failed();
      } else if (p.literal("true")) {
        v = {Value::Bool, 0, {}, true};
      } else if (p.literal("false")) {
        v = {Value::Bool, 0, {}, false};
      } else if (!p.integer(v.i)) {
        return failed();
      }
      obj.values_[key] = v;
    } while (p.eat(','));
    if (!p.eat('}')) return p.fail("expected '}'"), failed();
  }
  p.ws();
  if (p.i != text.size()) return p.fail("trailing characters"), failed();
  return obj;
}

std::optional<std::vector<std::int64_t>> parse_int_array(std::string_view text, std::string* error) {
  Parser p{text, 0, {}};
  std::vector<std::int64_t> out;
  auto failed = [&]() -> std::optional<std::vector<std::int64_t>> {
    if (error) *error = p.err.empty() ? "invalid JSON" : p.err;
    return std::nullopt;
  };
  if (!p.eat('[')) return p.fail("expected '['"), failed();
  if (!p.eat(']')) {
    do {
      std::int64_t v = 0;
      if (!p.integer(v)) return failed();
      out.push_back(v);
    } while (p.eat(','));
    if (!p.eat(']')) return p.fail("expected ']'"), failed();
  }
  p.ws();
  if (p.i != text.size()) return p.fail("trailing characters"), failed();
  return out;
}

std::optional<std::int64_t> FlatObject::integer(const std::string& key) const {
  const auto it = values_.find(key);
  if (it == values_.end() || it->second.kind != Value::Int) return std::nullopt;
  return it->second.i;
}

std::optional<std::string> FlatObject::string(const std::string& key) const {
  const auto it = values_.find(key);
  if (it == values_.end() || it->second.kind != Value::Str) return std::nullopt;
  return it->second.s;
}

std::optional<bool> FlatObject::boolean(const std::string& key) const {
  const auto it = values_.find(key);
  if (it == values_.end() || it->second.kind != Value::Bool) return std::nullopt;
  return it->second.b;
}

void append_string(std::string& out, std::string_view s) {
  out.push_back('"');
  for (const char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      default: out.push_back(c);
    }
  }
  out.push_back('"');
}

}  // namespace btb::json
