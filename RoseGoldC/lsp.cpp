#include "eval.h"
#include "interp.h"
#include "lexer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

extern std::string version;

namespace {

struct Json {
  enum class Kind { Null, Bool, Number, String, Array, Object };
  Kind kind = Kind::Null;
  bool b = false;
  bool integer = false;
  long long i = 0;
  double n = 0;
  std::string s;
  std::vector<Json> a;
  std::vector<std::pair<std::string, Json>> o;

  static Json null() { return {}; }
  static Json boolean(bool v) {
    Json j;
    j.kind = Kind::Bool;
    j.b = v;
    return j;
  }
  static Json num(long long v) {
    Json j;
    j.kind = Kind::Number;
    j.integer = true;
    j.i = v;
    j.n = static_cast<double>(v);
    return j;
  }
  static Json str(std::string v) {
    Json j;
    j.kind = Kind::String;
    j.s = std::move(v);
    return j;
  }
  static Json array() {
    Json j;
    j.kind = Kind::Array;
    return j;
  }
  static Json object() {
    Json j;
    j.kind = Kind::Object;
    return j;
  }

  Json &set(std::string key, Json value) {
    kind = Kind::Object;
    for (auto &kv : o) {
      if (kv.first == key) {
        kv.second = std::move(value);
        return kv.second;
      }
    }
    o.emplace_back(std::move(key), std::move(value));
    return o.back().second;
  }

  const Json *get(const char *key) const {
    if (kind != Kind::Object)
      return nullptr;
    for (const auto &kv : o) {
      if (kv.first == key)
        return &kv.second;
    }
    return nullptr;
  }

  std::string getStr(const char *key, const std::string &fallback = "") const {
    const Json *v = get(key);
    if (!v || v->kind != Kind::String)
      return fallback;
    return v->s;
  }

  const Json *getObj(const char *key) const {
    const Json *v = get(key);
    if (!v || v->kind != Kind::Object)
      return nullptr;
    return v;
  }

  const Json *getArr(const char *key) const {
    const Json *v = get(key);
    if (!v || v->kind != Kind::Array)
      return nullptr;
    return v;
  }
};

std::string jsonEscape(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 8);
  const char *hex = "0123456789abcdef";
  for (unsigned char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 0x20) {
        out += "\\u00";
        out.push_back(hex[c >> 4]);
        out.push_back(hex[c & 15]);
      } else {
        out.push_back(static_cast<char>(c));
      }
    }
  }
  return out;
}

std::string encode(const Json &j) {
  switch (j.kind) {
  case Json::Kind::Null:
    return "null";
  case Json::Kind::Bool:
    return j.b ? "true" : "false";
  case Json::Kind::Number:
    if (j.integer)
      return std::to_string(j.i);
    return std::to_string(j.n);
  case Json::Kind::String:
    return "\"" + jsonEscape(j.s) + "\"";
  case Json::Kind::Array: {
    std::string out = "[";
    for (size_t i = 0; i < j.a.size(); ++i) {
      if (i)
        out += ",";
      out += encode(j.a[i]);
    }
    out += "]";
    return out;
  }
  case Json::Kind::Object: {
    std::string out = "{";
    for (size_t i = 0; i < j.o.size(); ++i) {
      if (i)
        out += ",";
      out += "\"" + jsonEscape(j.o[i].first) + "\":" + encode(j.o[i].second);
    }
    out += "}";
    return out;
  }
  }
  return "null";
}

struct ParseError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct Parser {
  const std::string &src;
  size_t i = 0;
  explicit Parser(const std::string &s) : src(s) {}

  [[noreturn]] void fail(const char *msg) const { throw ParseError(msg); }

  char peek() const { return i < src.size() ? src[i] : '\0'; }

  char getc() {
    if (i >= src.size())
      fail("unexpected end of JSON");
    return src[i++];
  }

  void skip() {
    while (i < src.size() &&
           std::isspace(static_cast<unsigned char>(src[i])))
      ++i;
  }

  Json parseValue() {
    skip();
    char c = peek();
    if (c == '{')
      return parseObject();
    if (c == '[')
      return parseArray();
    if (c == '"')
      return parseString();
    if (c == 't' || c == 'f')
      return parseBool();
    if (c == 'n')
      return parseNull();
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
      return parseNumber();
    fail("expected JSON value");
  }

  Json parseObject() {
    if (getc() != '{')
      fail("expected '{'");
    Json obj = Json::object();
    skip();
    if (peek() == '}') {
      getc();
      return obj;
    }
    while (true) {
      skip();
      Json key = parseString();
      skip();
      if (getc() != ':')
        fail("expected ':'");
      obj.set(key.s, parseValue());
      skip();
      char c = getc();
      if (c == '}')
        return obj;
      if (c != ',')
        fail("expected ',' or '}'");
    }
  }

  Json parseArray() {
    if (getc() != '[')
      fail("expected '['");
    Json arr = Json::array();
    skip();
    if (peek() == ']') {
      getc();
      return arr;
    }
    while (true) {
      arr.a.push_back(parseValue());
      skip();
      char c = getc();
      if (c == ']')
        return arr;
      if (c != ',')
        fail("expected ',' or ']'");
    }
  }

  Json parseString() {
    if (getc() != '"')
      fail("expected string");
    std::string out;
    while (true) {
      char c = getc();
      if (c == '"')
        break;
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      char e = getc();
      switch (e) {
      case '"':
      case '\\':
      case '/':
        out.push_back(e);
        break;
      case 'b':
        out.push_back('\b');
        break;
      case 'f':
        out.push_back('\f');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'u': {
        unsigned cp = 0;
        for (int n = 0; n < 4; ++n) {
          char h = getc();
          cp <<= 4;
          if (h >= '0' && h <= '9')
            cp |= static_cast<unsigned>(h - '0');
          else if (h >= 'a' && h <= 'f')
            cp |= static_cast<unsigned>(h - 'a' + 10);
          else if (h >= 'A' && h <= 'F')
            cp |= static_cast<unsigned>(h - 'A' + 10);
          else
            fail("invalid \\u escape");
        }
        if (cp < 0x80)
          out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) {
          out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
          out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
          out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
          out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
          out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        break;
      }
      default:
        fail("invalid string escape");
      }
    }
    return Json::str(std::move(out));
  }

  Json parseBool() {
    if (src.compare(i, 4, "true") == 0) {
      i += 4;
      return Json::boolean(true);
    }
    if (src.compare(i, 5, "false") == 0) {
      i += 5;
      return Json::boolean(false);
    }
    fail("expected true or false");
  }

  Json parseNull() {
    if (src.compare(i, 4, "null") == 0) {
      i += 4;
      return Json::null();
    }
    fail("expected null");
  }

  Json parseNumber() {
    const size_t start = i;
    if (peek() == '-')
      getc();
    if (!std::isdigit(static_cast<unsigned char>(peek())))
      fail("expected number");
    if (peek() == '0')
      getc();
    else {
      while (std::isdigit(static_cast<unsigned char>(peek())))
        getc();
    }
    bool isInt = true;
    if (peek() == '.') {
      isInt = false;
      getc();
      if (!std::isdigit(static_cast<unsigned char>(peek())))
        fail("expected digit after '.'");
      while (std::isdigit(static_cast<unsigned char>(peek())))
        getc();
    }
    if (peek() == 'e' || peek() == 'E') {
      isInt = false;
      getc();
      if (peek() == '+' || peek() == '-')
        getc();
      if (!std::isdigit(static_cast<unsigned char>(peek())))
        fail("expected exponent digit");
      while (std::isdigit(static_cast<unsigned char>(peek())))
        getc();
    }
    const std::string text = src.substr(start, i - start);
    Json j;
    j.kind = Json::Kind::Number;
    if (isInt) {
      try {
        j.integer = true;
        j.i = std::stoll(text);
        j.n = static_cast<double>(j.i);
        return j;
      } catch (...) {
      }
    }
    j.integer = false;
    j.n = std::stod(text);
    return j;
  }
};

Json parseJson(const std::string &src) {
  Parser p(src);
  Json j = p.parseValue();
  p.skip();
  if (p.i != src.size())
    throw ParseError("trailing JSON");
  return j;
}

std::string percentDecode(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  auto hex = [](char c) {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      int a = hex(s[i + 1]);
      int b = hex(s[i + 2]);
      if (a >= 0 && b >= 0) {
        out.push_back(static_cast<char>((a << 4) | b));
        i += 2;
        continue;
      }
    }
    out.push_back(s[i]);
  }
  return out;
}

std::string uriToPath(std::string uri) {
  const std::string prefix = "file://";
  if (uri.size() < prefix.size() ||
      uri.compare(0, prefix.size(), prefix) != 0)
    return uri;
  uri = uri.substr(prefix.size());
  if (uri.size() >= 2 && uri[0] == '/' && uri[1] == '/') {
    const auto slash = uri.find('/', 2);
    if (slash != std::string::npos)
      uri = uri.substr(slash);
  }
  uri = percentDecode(uri);
#ifdef _WIN32
  if (uri.size() >= 3 && uri[0] == '/' &&
      std::isalpha(static_cast<unsigned char>(uri[1])) && uri[2] == ':')
    uri.erase(uri.begin());
  for (char &c : uri) {
    if (c == '/')
      c = '\\';
  }
#endif
  return uri;
}

std::vector<std::string> splitLines(const std::string &text) {
  std::vector<std::string> lines;
  std::string cur;
  for (char c : text) {
    if (c == '\n') {
      if (!cur.empty() && cur.back() == '\r')
        cur.pop_back();
      lines.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  lines.push_back(cur);
  return lines;
}

void tokenRange(const std::string &text, int line1, int col1, int &sl, int &sc,
                int &el, int &ec) {
  auto lines = splitLines(text);
  sl = line1 > 0 ? line1 - 1 : 0;
  sc = col1 > 0 ? col1 - 1 : 0;
  if (lines.empty()) {
    sl = sc = el = ec = 0;
    return;
  }
  if (sl >= static_cast<int>(lines.size()))
    sl = static_cast<int>(lines.size()) - 1;
  const std::string &line = lines[static_cast<size_t>(sl)];
  if (sc > static_cast<int>(line.size()))
    sc = static_cast<int>(line.size());
  el = sl;
  ec = sc;
  if (sc < static_cast<int>(line.size()) &&
      (std::isalnum(static_cast<unsigned char>(line[static_cast<size_t>(sc)])) ||
       line[static_cast<size_t>(sc)] == '_')) {
    while (ec < static_cast<int>(line.size()) &&
           (std::isalnum(
                static_cast<unsigned char>(line[static_cast<size_t>(ec)])) ||
            line[static_cast<size_t>(ec)] == '_'))
      ++ec;
  } else if (sc < static_cast<int>(line.size())) {
    ec = sc + 1;
  }
}

int jsonInt(const Json *o, const char *key, int fallback = 0) {
  if (!o)
    return fallback;
  const Json *v = o->get(key);
  if (!v || v->kind != Json::Kind::Number)
    return fallback;
  return v->integer ? static_cast<int>(v->i) : static_cast<int>(v->n);
}

std::string pathToUri(std::string path) {
#ifdef _WIN32
  for (char &c : path) {
    if (c == '\\')
      c = '/';
  }
#endif
  std::string enc;
  enc.reserve(path.size() + 16);
  for (unsigned char c : path) {
    if (std::isalnum(c) || c == '/' || c == ':' || c == '.' || c == '-' ||
        c == '_' || c == '~')
      enc.push_back(static_cast<char>(c));
    else {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "%%%02X", c);
      enc += buf;
    }
  }
#ifdef _WIN32
  if (enc.size() >= 2 && std::isalpha(static_cast<unsigned char>(enc[0])) &&
      enc[1] == ':')
    return "file:///" + enc;
#endif
  if (!enc.empty() && enc[0] != '/')
    return "file:///" + enc;
  return "file://" + enc;
}

std::string commentText(const std::string &line) {
  std::string t = line;
  auto a = t.find_first_not_of(" \t\r");
  if (a == std::string::npos)
    return "";
  t = t.substr(a);
  if (t.compare(0, 3, "///") == 0)
    return t.substr(3) == "" ? "" : [&] {
      auto s = t.substr(3);
      auto b = s.find_first_not_of(" \t");
      return b == std::string::npos ? std::string() : s.substr(b);
    }();
  if (t.compare(0, 2, "//") == 0) {
    auto s = t.substr(2);
    auto b = s.find_first_not_of(" \t");
    return b == std::string::npos ? std::string() : s.substr(b);
  }
  if (t.compare(0, 2, "##") == 0) {
    auto s = t.substr(2);
    auto b = s.find_first_not_of(" \t");
    return b == std::string::npos ? std::string() : s.substr(b);
  }
  if (!t.empty() && t[0] == '#' && (t.size() < 2 || t[1] != '/')) {
    auto s = t.substr(1);
    auto b = s.find_first_not_of(" \t");
    return b == std::string::npos ? std::string() : s.substr(b);
  }
  return std::string();
}

std::string docsAbove(const std::vector<std::string> &lines, int line0) {
  std::vector<std::string> docs;
  for (int i = line0 - 1; i >= 0; --i) {
    std::string t = lines[static_cast<size_t>(i)];
    auto a = t.find_first_not_of(" \t\r");
    std::string trimmed = a == std::string::npos ? "" : t.substr(a);
    if (!trimmed.empty() && trimmed[0] == '@')
      continue;
    std::string text = commentText(lines[static_cast<size_t>(i)]);
    if (!text.empty() || (a != std::string::npos &&
                          (trimmed.compare(0, 2, "//") == 0 ||
                           trimmed.compare(0, 1, "#") == 0))) {
      if (!text.empty())
        docs.insert(docs.begin(), text);
    } else if (trimmed.empty()) {
      if (!docs.empty())
        break;
    } else
      break;
  }
  std::string out;
  for (size_t i = 0; i < docs.size(); ++i) {
    if (i)
      out += "\n";
    out += docs[i];
  }
  return out;
}

std::string trimCopy(std::string s) {
  auto a = s.find_first_not_of(" \t\r");
  if (a == std::string::npos)
    return "";
  auto b = s.find_last_not_of(" \t\r");
  return s.substr(a, b - a + 1);
}

std::string codeLine(const std::string &line) {
  std::string out;
  bool inBlock = false;
  for (size_t i = 0; i < line.size(); ++i) {
    char a = line[i];
    char b = i + 1 < line.size() ? line[i + 1] : '\0';
    if (inBlock) {
      if (a == '#' && b == '/') {
        inBlock = false;
        ++i;
      }
      continue;
    }
    if (a == '/' && b == '#') {
      inBlock = true;
      ++i;
      continue;
    }
    if (a == '/' && b == '/')
      break;
    if (a == '#')
      break;
    out.push_back(a);
  }
  return trimCopy(out);
}

struct NavSymbol {
  std::string name;
  std::string kind;
  std::string detail;
  std::string doc;
  std::string uri;
  int line = 0;
  int col = 0;
  std::vector<std::string> params;
};

const char *declKindName(Tok k) {
  switch (k) {
  case Tok::Function:
    return "fn";
  case Tok::Signal:
    return "signal";
  case Tok::Struct:
    return "struct";
  case Tok::Class:
    return "class";
  case Tok::Trait:
    return "trait";
  case Tok::Enum:
    return "enum";
  case Tok::Variable:
    return "var";
  case Tok::Constant:
    return "const";
  default:
    return nullptr;
  }
}

void collectFromSource(const std::string &source, const std::string &uri,
                       std::vector<NavSymbol> &out) {
  std::vector<Token> tokens;
  try {
    tokens = tokenize(source, uriToPath(uri));
  } catch (...) {
    return;
  }
  auto lines = splitLines(source);
  for (size_t i = 0; i + 1 < tokens.size(); ++i) {
    const char *kind = declKindName(tokens[i].kind);
    if (!kind)
      continue;
    if (tokens[i + 1].kind != Tok::Identifier)
      continue;
    const Token &name = tokens[i + 1];
    NavSymbol s;
    s.name = name.text;
    s.kind = kind;
    s.uri = uri;
    s.line = name.line > 0 ? name.line - 1 : 0;
    s.col = name.col > 0 ? name.col - 1 : 0;
    if (s.line >= 0 && s.line < static_cast<int>(lines.size())) {
      s.detail = codeLine(lines[static_cast<size_t>(s.line)]);
      s.doc = docsAbove(lines, s.line);
    }
    if (s.kind == "fn" || s.kind == "signal") {
      size_t j = i + 2;
      if (j < tokens.size() && tokens[j].kind == Tok::LParen) {
        ++j;
        while (j < tokens.size() && tokens[j].kind != Tok::RParen &&
               tokens[j].kind != Tok::LBrace && tokens[j].kind != Tok::Semi) {
          if (tokens[j].kind == Tok::Identifier &&
              tokens[j - 1].kind != Tok::Colon &&
              tokens[j].text != "self")
            s.params.push_back(tokens[j].text);
          ++j;
        }
      }
    }
    out.push_back(std::move(s));
  }
}

void collectWorkspace(const std::string &rootPath,
                      const std::map<std::string, std::string> &docs,
                      std::vector<NavSymbol> &out) {
  std::set<std::string> seen;
  auto norm = [](std::string p) {
#ifdef _WIN32
    for (char &c : p)
      if (c == '/')
        c = '\\';
    for (char &c : p)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
    return p;
  };
  for (const auto &kv : docs) {
    collectFromSource(kv.second, kv.first, out);
    seen.insert(norm(uriToPath(kv.first)));
  }
  if (rootPath.empty())
    return;
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path root(rootPath);
  if (!fs::is_directory(root, ec))
    return;
  fs::recursive_directory_iterator it(
      root, fs::directory_options::skip_permission_denied, ec);
  fs::recursive_directory_iterator end;
  for (; it != end; it.increment(ec)) {
    if (ec) {
      ec.clear();
      continue;
    }
    const auto name = it->path().filename().string();
    if (it->is_directory(ec) && (name == "build" || name == ".git" ||
                                 name == "node_modules" || name == ".cursor")) {
      it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file(ec) || it->path().extension() != ".rg")
      continue;
    const std::string path = it->path().string();
    if (seen.count(norm(path)))
      continue;
    try {
      collectFromSource(readFile(path), pathToUri(path), out);
    } catch (...) {
    }
  }
}

struct IdentAt {
  std::string name;
  std::string qualifier;
  int line = 0;
  int col = 0;
  int endCol = 0;
};

bool identAt(const std::string &source, int line0, int col0, IdentAt &out) {
  std::vector<Token> tokens;
  try {
    tokens = tokenize(source, "");
  } catch (...) {
    return false;
  }
  const int line1 = line0 + 1;
  const int col1 = col0 + 1;
  for (size_t i = 0; i < tokens.size(); ++i) {
    const Token &t = tokens[i];
    if (t.kind != Tok::Identifier && t.kind != Tok::True &&
        t.kind != Tok::False)
      continue;
    if (t.line != line1)
      continue;
    const int start = t.col;
    const int end = t.col + static_cast<int>(t.text.size());
    if (col1 < start || col1 > end)
      continue;
    out.name = t.text;
    out.line = line0;
    out.col = start > 0 ? start - 1 : 0;
    out.endCol = end > 0 ? end - 1 : 0;
    if (i >= 2 && tokens[i - 1].kind == Tok::Dot &&
        tokens[i - 2].kind == Tok::Identifier)
      out.qualifier = tokens[i - 2].text;
    return true;
  }
  return false;
}

std::string linePrefixAt(const std::string &source, int line0, int char0) {
  auto lines = splitLines(source);
  if (line0 < 0 || line0 >= static_cast<int>(lines.size()))
    return "";
  const std::string &line = lines[static_cast<size_t>(line0)];
  int n = char0;
  if (n < 0)
    n = 0;
  if (n > static_cast<int>(line.size()))
    n = static_cast<int>(line.size());
  return line.substr(0, static_cast<size_t>(n));
}

int completionKind(const std::string &kind) {
  if (kind == "fn")
    return 3;
  if (kind == "signal")
    return 23;
  if (kind == "struct")
    return 22;
  if (kind == "class")
    return 7;
  if (kind == "enum")
    return 13;
  if (kind == "trait")
    return 8;
  if (kind == "var")
    return 6;
  if (kind == "const")
    return 21;
  if (kind == "keyword")
    return 14;
  if (kind == "type")
    return 25;
  if (kind == "method")
    return 2;
  return 3;
}

Json pos(int line, int character);

Json lspRange(int sl, int sc, int el, int ec) {
  Json range = Json::object();
  range.set("start", pos(sl, sc));
  range.set("end", pos(el, ec));
  return range;
}

Json hoverPayload(const NavSymbol &s, const IdentAt *ident) {
  std::string md = "```rosegold\n" +
                   (s.detail.empty() ? s.kind + " " + s.name : s.detail) +
                   "\n```";
  if (!s.doc.empty())
    md += "\n\n" + s.doc;
  Json contents = Json::object();
  contents.set("kind", Json::str("markdown"));
  contents.set("value", Json::str(md));
  Json hover = Json::object();
  hover.set("contents", std::move(contents));
  if (ident)
    hover.set("range", lspRange(ident->line, ident->col, ident->line,
                                ident->endCol));
  return hover;
}

Json locationPayload(const NavSymbol &s) {
  Json loc = Json::object();
  loc.set("uri", Json::str(s.uri));
  const int end = s.col + static_cast<int>(s.name.size());
  loc.set("range", lspRange(s.line, s.col, s.line, end));
  return loc;
}

Json completionItem(const std::string &label, int kind, const std::string &detail,
                    const std::string &doc, const std::string &insert) {
  Json it = Json::object();
  it.set("label", Json::str(label));
  it.set("kind", Json::num(kind));
  if (!detail.empty())
    it.set("detail", Json::str(detail));
  if (!doc.empty())
    it.set("documentation", Json::str(doc));
  if (!insert.empty() && insert != label) {
    it.set("insertText", Json::str(insert));
    it.set("insertTextFormat", Json::num(2));
  }
  return it;
}

std::string fnSnippet(const std::string &name,
                      const std::vector<std::string> &params) {
  std::string out = name + "(";
  for (size_t i = 0; i < params.size(); ++i) {
    if (i)
      out += ", ";
    out += "${" + std::to_string(i + 1) + ":" + params[i] + "}";
  }
  out += ")";
  return out;
}

const char *kKeywords[] = {
    "fn",      "var",  "const",    "struct", "data",    "class",   "trait",
    "extends",
    "enum",    "mod",  "impl",     "pub",    "private",  "protected",
    "abstract","final","match",    "switch", "for",
    "in",      "super","signal",   "import", "from",     "as",     "return",
    "pass",    "break","continue", "if",     "elif",     "else",   "while",
    "self",    "true", "false",
    "try",     "do",   "throws",   "throw",  "catch"};
const char *kTypes[] = {"Int",  "Float", "String", "Bool", "Void",
                        "Array", "Map",  "Range",  "UUID", "Vec2", "Vec3"};

struct Builtin {
  const char *label;
  const char *insert;
  const char *detail;
};
const Builtin kBuiltins[] = {
    {"print", "print($0)", "print(...)"},
    {"assert", "assert($0)", "assert(cond)"},
    {"len", "len($0)", "len(xs) — Array, String, or Map"},
    {"checks.eq", "checks.eq($1, $2)", "checks.eq(a, b)"},
    {"checks.neq", "checks.neq($1, $2)", "checks.neq(a, b)"},
    {"checks.eq_string", "checks.eq_string($1, $2)", "checks.eq_string(a, b)"},
    {"checks.that", "checks.that($0)", "checks.that(cond)"},
    {"argv", "argv($0)", "argv(i) — script path at 0"},
    {"argv_len", "argv_len()", "argv_len() — argc"},
};

Json pos(int line, int character) {
  Json p = Json::object();
  p.set("line", Json::num(line));
  p.set("character", Json::num(character));
  return p;
}

Json lspDiagnostic(const Diagnostic &d, const std::string &text) {
  int sl = 0, sc = 0, el = 0, ec = 0;
  tokenRange(text, d.line, d.col, sl, sc, el, ec);
  Json range = Json::object();
  range.set("start", pos(sl, sc));
  range.set("end", pos(el, ec));
  Json item = Json::object();
  item.set("range", std::move(range));
  item.set("severity",
           Json::num(d.severity == "warning" ? 2 : 1));
  item.set("source", Json::str("rosegoldc"));
  item.set("message", Json::str(d.message));
  return item;
}

void writeMessage(const std::string &body) {
  const std::string header =
      "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
  fwrite(header.data(), 1, header.size(), stdout);
  fwrite(body.data(), 1, body.size(), stdout);
  fflush(stdout);
}

void writeResponse(const Json *id, Json result) {
  Json msg = Json::object();
  msg.set("jsonrpc", Json::str("2.0"));
  if (id)
    msg.set("id", *id);
  else
    msg.set("id", Json::null());
  msg.set("result", std::move(result));
  writeMessage(encode(msg));
}

void writeError(const Json *id, int code, const std::string &message) {
  Json err = Json::object();
  err.set("code", Json::num(code));
  err.set("message", Json::str(message));
  Json msg = Json::object();
  msg.set("jsonrpc", Json::str("2.0"));
  if (id)
    msg.set("id", *id);
  else
    msg.set("id", Json::null());
  msg.set("error", std::move(err));
  writeMessage(encode(msg));
}

void writeNotification(const std::string &method, Json params) {
  Json msg = Json::object();
  msg.set("jsonrpc", Json::str("2.0"));
  msg.set("method", Json::str(method));
  msg.set("params", std::move(params));
  writeMessage(encode(msg));
}

bool readMessage(std::string &body) {
  std::string headers;
    int consecutiveNewlines = 0;
    while (true) {
      const int c = fgetc(stdin);
      if (c == EOF)
        return false;
      headers.push_back(static_cast<char>(c));
      if (c == '\n') {
        ++consecutiveNewlines;
        if (consecutiveNewlines >= 2)
          break;
      } else if (c != '\r') {
        consecutiveNewlines = 0;
      }
      if (headers.size() > 65536)
        throw ParseError("LSP header too large");
    }

  size_t length = 0;
  bool found = false;
  size_t lineStart = 0;
  for (size_t n = 0; n <= headers.size(); ++n) {
    if (n == headers.size() || headers[n] == '\n') {
      std::string line = headers.substr(lineStart, n - lineStart);
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      const std::string key = "content-length:";
      std::string lower = line;
      for (char &ch : lower)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      if (lower.compare(0, key.size(), key) == 0) {
        size_t v = key.size();
        while (v < line.size() &&
               std::isspace(static_cast<unsigned char>(line[v])))
          ++v;
        length = static_cast<size_t>(std::strtoull(line.c_str() + v, nullptr, 10));
        found = true;
      }
      lineStart = n + 1;
    }
  }
  if (!found)
    throw ParseError("missing Content-Length");

  body.assign(length, '\0');
  size_t got = 0;
  while (got < length) {
    const size_t n = fread(&body[got], 1, length - got, stdin);
    if (n == 0)
      return false;
    got += n;
  }
  return true;
}

Json initializeResult() {
  Json sync = Json::object();
  sync.set("openClose", Json::boolean(true));
  sync.set("change", Json::num(1));
  Json caps = Json::object();
  caps.set("textDocumentSync", std::move(sync));
  caps.set("positionEncoding", Json::str("utf-16"));
  caps.set("hoverProvider", Json::boolean(true));
  caps.set("definitionProvider", Json::boolean(true));
  Json completion = Json::object();
  Json triggers = Json::array();
  triggers.a.push_back(Json::str("."));
  triggers.a.push_back(Json::str("@"));
  triggers.a.push_back(Json::str(":"));
  completion.set("triggerCharacters", std::move(triggers));
  caps.set("completionProvider", std::move(completion));
  Json info = Json::object();
  info.set("name", Json::str("RoseGoldC"));
  info.set("version", Json::str(version));
  Json result = Json::object();
  result.set("capabilities", std::move(caps));
  result.set("serverInfo", std::move(info));
  return result;
}

void publishDiagnostics(const std::string &uri, const std::string &text,
                        const std::string &path) {
  auto diags = checkSource(text, path.empty() ? uri : path);
  Json arr = Json::array();
  arr.a.reserve(diags.size());
  for (const auto &d : diags)
    arr.a.push_back(lspDiagnostic(d, text));
  Json params = Json::object();
  params.set("uri", Json::str(uri));
  params.set("diagnostics", std::move(arr));
  writeNotification("textDocument/publishDiagnostics", std::move(params));
}

size_t offsetAt(const std::string &text, int line, int character) {
  int ln = 0;
  size_t i = 0;
  while (ln < line && i < text.size()) {
    if (text[i] == '\n')
      ++ln;
    ++i;
  }
  int col = 0;
  while (col < character && i < text.size() && text[i] != '\n') {
    ++i;
    ++col;
  }
  return i;
}

std::string applyChange(std::string text, const Json &change) {
  const Json *range = change.getObj("range");
  std::string next = change.getStr("text");
  if (!range)
    return next;
  const Json *start = range->getObj("start");
  const Json *end = range->getObj("end");
  int sl = start && start->get("line") && start->get("line")->integer
               ? static_cast<int>(start->get("line")->i)
               : 0;
  int sc = start && start->get("character") && start->get("character")->integer
               ? static_cast<int>(start->get("character")->i)
               : 0;
  int el = end && end->get("line") && end->get("line")->integer
               ? static_cast<int>(end->get("line")->i)
               : sl;
  int ec = end && end->get("character") && end->get("character")->integer
               ? static_cast<int>(end->get("character")->i)
               : sc;
  const size_t a = offsetAt(text, sl, sc);
  const size_t b = offsetAt(text, el, ec);
  if (a > text.size() || b > text.size() || a > b)
    return next.empty() ? text : next;
  return text.substr(0, a) + next + text.substr(b);
}

struct Server {
  std::map<std::string, std::string> docs;
  std::string rootPath;
  bool shuttingDown = false;

  std::string docText(const std::string &uri) const {
    auto it = docs.find(uri);
    if (it != docs.end())
      return it->second;
    try {
      return readFile(uriToPath(uri));
    } catch (...) {
      return "";
    }
  }

  std::vector<NavSymbol> symbols() const {
    std::vector<NavSymbol> out;
    collectWorkspace(rootPath, docs, out);
    return out;
  }

  Json hover(const Json &params) {
    const Json *td = params.getObj("textDocument");
    const Json *position = params.getObj("position");
    if (!td || !position)
      return Json::null();
    const std::string uri = td->getStr("uri");
    const std::string text = docText(uri);
    if (text.empty() && !docs.count(uri))
      return Json::null();
    const int line = jsonInt(position, "line");
    const int character = jsonInt(position, "character");
    IdentAt ident;
    if (!identAt(text, line, character, ident))
      return Json::null();

    if (ident.qualifier == "checks") {
      NavSymbol s;
      s.name = ident.name;
      s.kind = "fn";
      s.detail = "checks." + ident.name;
      return hoverPayload(s, &ident);
    }
    if (ident.qualifier == "process") {
      NavSymbol s;
      s.name = ident.name;
      s.kind = "fn";
      s.detail = "process." + ident.name;
      return hoverPayload(s, &ident);
    }
    for (const auto &b : kBuiltins) {
      if (ident.qualifier.empty() && ident.name == b.label) {
        NavSymbol s;
        s.name = b.label;
        s.kind = "fn";
        s.detail = b.detail;
        return hoverPayload(s, &ident);
      }
    }

    auto all = symbols();
    const NavSymbol *hit = nullptr;
    for (const auto &s : all) {
      if (s.name != ident.name)
        continue;
      if (s.uri == uri) {
        hit = &s;
        break;
      }
      if (!hit)
        hit = &s;
    }
    if (!hit)
      return Json::null();
    return hoverPayload(*hit, &ident);
  }

  Json definition(const Json &params) {
    const Json *td = params.getObj("textDocument");
    const Json *position = params.getObj("position");
    if (!td || !position)
      return Json::null();
    const std::string uri = td->getStr("uri");
    const std::string text = docText(uri);
    IdentAt ident;
    if (!identAt(text, jsonInt(position, "line"),
                 jsonInt(position, "character"), ident))
      return Json::null();
    Json arr = Json::array();
    for (const auto &s : symbols()) {
      if (s.name == ident.name)
        arr.a.push_back(locationPayload(s));
    }
    if (arr.a.empty())
      return Json::null();
    return arr;
  }

  Json completion(const Json &params) {
    const Json *td = params.getObj("textDocument");
    const Json *position = params.getObj("position");
    if (!td || !position)
      return Json::null();
    const std::string uri = td->getStr("uri");
    const std::string text = docText(uri);
    const std::string prefix =
        linePrefixAt(text, jsonInt(position, "line"),
                     jsonInt(position, "character"));
    Json items = Json::array();
    auto all = symbols();

    auto reAt = [&]() {
      if (prefix.empty())
        return false;
      size_t i = prefix.size();
      while (i > 0 && (std::isalnum(static_cast<unsigned char>(prefix[i - 1])) ||
                       prefix[i - 1] == '_'))
        --i;
      return i > 0 && prefix[i - 1] == '@';
    };
    auto reColonType = [&]() {
      size_t i = prefix.size();
      while (i > 0 && (std::isalnum(static_cast<unsigned char>(prefix[i - 1])) ||
                       prefix[i - 1] == '_'))
        --i;
      while (i > 0 && std::isspace(static_cast<unsigned char>(prefix[i - 1])))
        --i;
      return i > 0 && prefix[i - 1] == ':';
    };
    auto memberRecv = [&]() -> std::string {
      size_t i = prefix.size();
      while (i > 0 && (std::isalnum(static_cast<unsigned char>(prefix[i - 1])) ||
                       prefix[i - 1] == '_'))
        --i;
      if (i == 0 || prefix[i - 1] != '.')
        return "";
      size_t end = i - 1;
      size_t start = end;
      while (start > 0 &&
             (std::isalnum(static_cast<unsigned char>(prefix[start - 1])) ||
              prefix[start - 1] == '_'))
        --start;
      return prefix.substr(start, end - start);
    };

    if (reAt()) {
      items.a.push_back(completionItem("test", 14, "@test",
                                       "Mark a test function", "test"));
      items.a.push_back(completionItem(
          "deprecated", 14, "@deprecated", "Warn when this function is called",
          "deprecated"));
      items.a.push_back(completionItem(
          "constexpr", 14, "@constexpr",
          "Mark a pure function (checked at load)", "constexpr"));
      items.a.push_back(completionItem(
          "ufcs", 14, "@ufcs", "Call as x.fn(args) meaning fn(x, args)",
          "ufcs"));
      items.a.push_back(completionItem(
          "optional", 14, "@optional",
          "Field may be omitted; filled with a zero value", "optional"));
      Json list = Json::object();
      list.set("isIncomplete", Json::boolean(false));
      list.set("items", std::move(items));
      return list;
    }

    const std::string recv = memberRecv();
    if (!recv.empty()) {
      if (recv == "std") {
        items.a.push_back(
            completionItem("math", 9, "std.math", "Int/Float math", "math"));
        items.a.push_back(
            completionItem("str", 9, "std.str", "String helpers", "str"));
        items.a.push_back(completionItem(
            "io", 9, "std.io", "Files (read/write throws)", "io"));
        items.a.push_back(
            completionItem("vec", 9, "std.vec", "Vec2 / Vec3", "vec"));
        items.a.push_back(
            completionItem("v4", 3, "std.v4()", "Random UUID v4", "v4()"));
        items.a.push_back(
            completionItem("nil", 3, "std.nil()", "All-zero UUID", "nil()"));
        items.a.push_back(completionItem(
            "parse", 3, "std.parse(s) throws", "Parse a UUID string",
            "parse($0)"));
        items.a.push_back(completionItem(
            "valid", 3, "std.valid(s)", "True if s is a UUID", "valid($0)"));
        items.a.push_back(
            completionItem("UUID", 22, "data UUID", "Frozen UUID", "UUID"));
      } else if (recv == "math") {
        const Builtin mathFns[] = {
            {"abs", "abs($0)", "math.abs(n)"},
            {"sign", "sign($0)", "math.sign(n)"},
            {"min", "min($1, $2)", "math.min(a, b)"},
            {"max", "max($1, $2)", "math.max(a, b)"},
            {"clamp", "clamp($1, $2, $3)", "math.clamp(v, lo, hi)"},
            {"gcd", "gcd($1, $2)", "math.gcd(a, b)"},
            {"pow", "pow($1, $2)", "math.pow(a, b) — Int"},
            {"rand_int", "rand_int($0)", "math.rand_int(n)"},
            {"sin", "sin($0)", "math.sin(n)"},
            {"cos", "cos($0)", "math.cos(n)"},
            {"atan2", "atan2($1, $2)", "math.atan2(y, x)"},
            {"sqrt", "sqrt($0)", "math.sqrt(n)"},
            {"powf", "powf($1, $2)", "math.powf(a, b) — Float"},
            {"to_int", "to_int($0)", "math.to_int(n)"},
            {"to_float", "to_float($0)", "math.to_float(n)"},
            {"floor", "floor($0)", "math.floor(n)"},
            {"ceil", "ceil($0)", "math.ceil(n)"},
            {"random", "random()", "math.random() — 0..1"},
            {"lerp", "lerp($1, $2, $3)", "math.lerp(a, b, t)"},
            {"move_toward", "move_toward($1, $2, $3)",
             "math.move_toward(current, target, delta)"},
        };
        for (const auto &b : mathFns)
          items.a.push_back(
              completionItem(b.label, 2, b.detail, "", b.insert));
      } else if (recv == "str") {
        const Builtin strFns[] = {
            {"contains", "contains($1, $2)", "str.contains(s, sub)"},
            {"starts_with", "starts_with($1, $2)", "str.starts_with(s, prefix)"},
            {"ends_with", "ends_with($1, $2)", "str.ends_with(s, suffix)"},
            {"length", "length($0)", "str.length(s)"},
            {"is_empty", "is_empty($0)", "str.is_empty(s)"},
            {"repeat", "repeat($1, $2)", "str.repeat(s, n)"},
            {"upper", "upper($0)", "str.upper(s)"},
            {"lower", "lower($0)", "str.lower(s)"},
            {"trim", "trim($0)", "str.trim(s)"},
            {"slice", "slice($1, $2, $3)", "str.slice(s, start, end)"},
            {"split", "split($1, $2)", "str.split(s, sep)"},
            {"replace", "replace($1, $2, $3)", "str.replace(s, old, with)"},
            {"find", "find($1, $2)", "str.find(s, sub) — index or -1"},
        };
        for (const auto &b : strFns)
          items.a.push_back(
              completionItem(b.label, 2, b.detail, "", b.insert));
      } else if (recv == "io") {
        items.a.push_back(
            completionItem("exists", 2, "io.exists(path)", "", "exists($0)"));
        items.a.push_back(
            completionItem("remove", 2, "io.remove(path)", "", "remove($0)"));
        items.a.push_back(completionItem(
            "read_text", 2, "io.read_text(path) throws", "",
            "read_text($0)"));
        items.a.push_back(completionItem(
            "read_lines", 2, "io.read_lines(path) throws", "",
            "read_lines($0)"));
        items.a.push_back(completionItem(
            "write_text", 2, "io.write_text(path, content) throws", "",
            "write_text($1, $2)"));
      } else if (recv == "vec") {
        items.a.push_back(
            completionItem("Vec2", 7, "class Vec2", "Vec2 { x, y }", "Vec2"));
        items.a.push_back(completionItem(
            "Vec3", 7, "class Vec3", "Vec3 { x, y, z } extends Vec2", "Vec3"));
      } else if (recv == "checks") {
        items.a.push_back(
            completionItem("eq", 2, "checks.eq(a, b)", "", "eq($1, $2)"));
        items.a.push_back(
            completionItem("neq", 2, "checks.neq(a, b)", "", "neq($1, $2)"));
        items.a.push_back(completionItem("eq_string", 2,
                                         "checks.eq_string(a, b)", "",
                                         "eq_string($1, $2)"));
        items.a.push_back(
            completionItem("that", 2, "checks.that(cond)", "", "that($0)"));
      } else if (recv == "process") {
        items.a.push_back(completionItem("argv", 2, "process.argv(i)",
                                         "Script path at 0, then run args",
                                         "argv($0)"));
        items.a.push_back(completionItem("argc", 2, "process.argc()",
                                         "Number of argv entries", "argc()"));
      } else {
        for (const auto &s : all) {
          if (s.name == recv && s.kind == "signal") {
            items.a.push_back(completionItem("connect", 2,
                                             recv + ".connect(fn)", s.doc,
                                             "connect($0)"));
            items.a.push_back(completionItem(
                "emit", 2, recv + ".emit()", s.doc, fnSnippet("emit", s.params)));
            items.a.push_back(completionItem("disconnect", 2,
                                             recv + ".disconnect(fn)", s.doc,
                                             "disconnect($0)"));
            break;
          }
        }
      }
      Json list = Json::object();
      list.set("isIncomplete", Json::boolean(false));
      list.set("items", std::move(items));
      return list;
    }

    if (reColonType()) {
      for (const char *t : kTypes)
        items.a.push_back(completionItem(t, 25, "type", "", t));
      Json list = Json::object();
      list.set("isIncomplete", Json::boolean(false));
      list.set("items", std::move(items));
      return list;
    }

    for (const char *kw : kKeywords)
      items.a.push_back(completionItem(kw, 14, "keyword", "", kw));
    for (const char *t : kTypes)
      items.a.push_back(completionItem(t, 25, "type", "", t));
    for (const auto &b : kBuiltins)
      items.a.push_back(completionItem(b.label, 3, b.detail, "", b.insert));
    std::set<std::string> seen;
    for (const auto &s : all) {
      const std::string key = s.kind + ":" + s.name;
      if (!seen.insert(key).second)
        continue;
      std::string insert = s.name;
      if (s.kind == "fn")
        insert = fnSnippet(s.name, s.params);
      items.a.push_back(completionItem(s.name, completionKind(s.kind),
                                       s.detail, s.doc, insert));
    }
    Json list = Json::object();
    list.set("isIncomplete", Json::boolean(false));
    list.set("items", std::move(items));
    return list;
  }

  void didOpen(const Json &params) {
    const Json *td = params.getObj("textDocument");
    if (!td)
      return;
    const std::string uri = td->getStr("uri");
    const std::string text = td->getStr("text");
    docs[uri] = text;
    publishDiagnostics(uri, text, uriToPath(uri));
  }

  void didChange(const Json &params) {
    const Json *td = params.getObj("textDocument");
    if (!td)
      return;
    const std::string uri = td->getStr("uri");
    const Json *changes = params.getArr("contentChanges");
    if (!changes || changes->a.empty())
      return;
    std::string text = docs.count(uri) ? docs[uri] : "";
    for (const auto &ch : changes->a)
      text = applyChange(text, ch);
    docs[uri] = text;
    publishDiagnostics(uri, text, uriToPath(uri));
  }

  void didClose(const Json &params) {
    const Json *td = params.getObj("textDocument");
    if (!td)
      return;
    const std::string uri = td->getStr("uri");
    docs.erase(uri);
    Json arr = Json::array();
    Json p = Json::object();
    p.set("uri", Json::str(uri));
    p.set("diagnostics", std::move(arr));
    writeNotification("textDocument/publishDiagnostics", std::move(p));
  }

  void handle(const Json &msg) {
    const std::string method = msg.getStr("method");
    const Json *id = msg.get("id");
    const bool isReq = id && id->kind != Json::Kind::Null;
    const Json *params = msg.get("params");
    Json empty = Json::object();
    const Json &p = params ? *params : empty;

    if (method == "initialize") {
      rootPath = uriToPath(p.getStr("rootUri"));
      const Json *folders = p.getArr("workspaceFolders");
      if (rootPath.empty() && folders && !folders->a.empty())
        rootPath = uriToPath(folders->a[0].getStr("uri"));
      writeResponse(id, initializeResult());
      return;
    }
    if (method == "initialized" || method == "$/cancelRequest")
      return;
    if (method == "shutdown") {
      shuttingDown = true;
      writeResponse(id, Json::null());
      return;
    }
    if (method == "exit")
      return;
    if (method == "textDocument/didOpen") {
      didOpen(p);
      return;
    }
    if (method == "textDocument/didChange") {
      didChange(p);
      return;
    }
    if (method == "textDocument/didClose" || method == "textDocument/didSave") {
      if (method == "textDocument/didClose")
        didClose(p);
      return;
    }
    if (method == "textDocument/hover") {
      writeResponse(id, hover(p));
      return;
    }
    if (method == "textDocument/definition") {
      writeResponse(id, definition(p));
      return;
    }
    if (method == "textDocument/completion") {
      writeResponse(id, completion(p));
      return;
    }
    if (isReq)
      writeError(id, -32601, "Method not found: " + method);
  }
};

} // namespace

bool jsonRpcSelfTest() {
  try {
    Json j = parseJson(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"textDocument/"
        "didOpen\",\"params\":{\"textDocument\":{\"uri\":\"file:///t.rg\","
        "\"text\":\"fn main(): Int {\\n    return 0;\\n}\\n\"}}}");
    if (j.getStr("method") != "textDocument/didOpen")
      return false;
    const Json *td = j.getObj("params")
                         ? j.getObj("params")->getObj("textDocument")
                         : nullptr;
    if (!td || td->getStr("uri") != "file:///t.rg")
      return false;
    if (!j.get("id") || !j.get("id")->integer || j.get("id")->i != 1)
      return false;
    if (encode(*j.get("id")) != "1")
      return false;
    if (encode(Json::str("a\"b\\c")) != "\"a\\\"b\\\\c\"")
      return false;
    if (uriToPath("file:///C:/CODE/x.rg") !=
#ifdef _WIN32
        "C:\\CODE\\x.rg"
#else
        "/C:/CODE/x.rg"
#endif
    )
      return false;
    std::vector<NavSymbol> syms;
    collectFromSource("/// add two numbers\nfn add(a: Int, b: Int): Int {\n    return a + b;\n}\n",
                      "file:///t.rg", syms);
    if (syms.size() != 1 || syms[0].name != "add" || syms[0].kind != "fn" ||
        syms[0].params.size() != 2 || syms[0].params[0] != "a" ||
        syms[0].doc.find("add two") == std::string::npos)
      return false;
    IdentAt ident;
    if (!identAt("fn main(): Int { return add(1, 2); }\n", 0, 24, ident) ||
        ident.name != "add")
      return false;
    return true;
  } catch (...) {
    return false;
  }
}

int runLanguageServer() {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  setvbuf(stdin, nullptr, _IONBF, 0);
  setvbuf(stdout, nullptr, _IONBF, 0);

  Server server;
  try {
    while (true) {
      std::string body;
      if (!readMessage(body))
        break;
      Json msg;
      try {
        msg = parseJson(body);
      } catch (const std::exception &ex) {
        fprintf(stderr, "lsp: bad JSON: %s\n", ex.what());
        fflush(stderr);
        continue;
      }
      const std::string method = msg.getStr("method");
      server.handle(msg);
      if (method == "exit")
        return server.shuttingDown ? 0 : 1;
    }
  } catch (const std::exception &ex) {
    fprintf(stderr, "lsp: %s\n", ex.what());
    fflush(stderr);
    return 1;
  }
  return server.shuttingDown ? 0 : 1;
}
