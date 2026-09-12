#include "eval.h"
#include "interp.h"
#include "lexer.h"
#include "parser.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
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
  int endLine = 0;
  int endCol = 0;
  std::vector<std::string> params;
  bool throws = false;
  bool deprecated = false;
  bool isTest = false;
};

const char *declKindName(Tok k) {
  switch (k) {
  case Tok::Function:
    return "fn";
  case Tok::Signal:
    return "signal";
  case Tok::Struct:
    return "struct";
  case Tok::Data:
    return "data";
  case Tok::Class:
    return "class";
  case Tok::Trait:
    return "trait";
  case Tok::Enum:
    return "enum";
  case Tok::Module:
    return "mod";
  case Tok::Variable:
    return "var";
  case Tok::Constant:
    return "const";
  default:
    return nullptr;
  }
}

bool spanToClose(const std::vector<Token> &tokens, size_t from, int &el,
                 int &ec) {
  int depth = 0;
  bool inBrace = false;
  for (size_t i = from; i < tokens.size(); ++i) {
    const Tok k = tokens[i].kind;
    if (k == Tok::LBrace) {
      ++depth;
      inBrace = true;
    } else if (k == Tok::RBrace) {
      if (depth > 0)
        --depth;
      if (inBrace && depth == 0) {
        el = tokens[i].line > 0 ? tokens[i].line - 1 : 0;
        ec = tokens[i].col > 0 ? tokens[i].col : 0;
        return true;
      }
    } else if (!inBrace && k == Tok::Semi) {
      el = tokens[i].line > 0 ? tokens[i].line - 1 : 0;
      ec = tokens[i].col > 0 ? tokens[i].col : 0;
      return true;
    }
  }
  return false;
}

bool canOutlineParent(const std::string &kind) {
  return kind == "struct" || kind == "data" || kind == "class" ||
         kind == "trait" || kind == "enum" || kind == "mod";
}

int lspSymbolKind(const std::string &kind) {
  if (kind == "fn")
    return 12;
  if (kind == "signal")
    return 24;
  if (kind == "struct" || kind == "data")
    return 23;
  if (kind == "class")
    return 5;
  if (kind == "trait")
    return 11;
  if (kind == "enum")
    return 10;
  if (kind == "mod")
    return 2;
  if (kind == "var")
    return 13;
  if (kind == "const")
    return 14;
  return 13;
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
    s.endLine = s.line;
    s.endCol = s.col + static_cast<int>(s.name.size());
    spanToClose(tokens, i, s.endLine, s.endCol);
    if (s.endLine < s.line ||
        (s.endLine == s.line && s.endCol < s.col + static_cast<int>(s.name.size()))) {
      s.endLine = s.line;
      s.endCol = s.col + static_cast<int>(s.name.size());
    }
    if (s.line >= 0 && s.line < static_cast<int>(lines.size())) {
      s.detail = codeLine(lines[static_cast<size_t>(s.line)]);
      s.doc = docsAbove(lines, s.line);
    }
    if (tokens[i].kind == Tok::Function) {
      size_t k = i;
      while (k > 0) {
        const Tok pk = tokens[k - 1].kind;
        if (pk == Tok::Pub || pk == Tok::Private || pk == Tok::Protected ||
            pk == Tok::Abstract || pk == Tok::Final) {
          --k;
          continue;
        }
        if (k >= 2 && tokens[k - 2].kind == Tok::At &&
            tokens[k - 1].kind == Tok::Identifier) {
          if (tokens[k - 1].text == "deprecated")
            s.deprecated = true;
          if (tokens[k - 1].text == "test")
            s.isTest = true;
          k -= 2;
          continue;
        }
        break;
      }
    }
    if (s.kind == "fn" || s.kind == "signal") {
      size_t j = i + 2;
      if (j < tokens.size() && tokens[j].kind == Tok::LParen) {
        ++j;
        while (j < tokens.size() && tokens[j].kind != Tok::RParen &&
               tokens[j].kind != Tok::LBrace && tokens[j].kind != Tok::Semi) {
          if (tokens[j].kind == Tok::Identifier &&
              tokens[j - 1].kind != Tok::Colon &&
              tokens[j].text != "self") {
            std::string p = tokens[j].text;
            if (j + 2 < tokens.size() && tokens[j + 1].kind == Tok::Colon &&
                tokens[j + 2].kind == Tok::Identifier) {
              p += ": " + tokens[j + 2].text;
              size_t t = j + 3;
              if (t + 2 < tokens.size() && tokens[t].kind == Tok::LBracket &&
                  tokens[t + 1].kind == Tok::Identifier &&
                  tokens[t + 2].kind == Tok::RBracket)
                p += "[" + tokens[t + 1].text + "]";
            }
            s.params.push_back(std::move(p));
          }
          ++j;
        }
        if (j < tokens.size() && tokens[j].kind == Tok::RParen)
          ++j;
        if (j < tokens.size() && tokens[j].kind == Tok::Throws)
          s.throws = true;
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

struct CallSite {
  std::string name;
  std::string qualifier;
  int activeParam = 0;
};

bool callSiteAt(const std::string &source, int line0, int col0, CallSite &out) {
  std::vector<Token> tokens;
  try {
    tokens = tokenize(source, "");
  } catch (...) {
    return false;
  }
  const int line1 = line0 + 1;
  const int col1 = col0 + 1;
  struct Frame {
    std::string name;
    std::string qualifier;
    int param = 0;
  };
  std::vector<Frame> stack;
  std::string pending;
  std::string pendingQual;
  for (size_t i = 0; i < tokens.size(); ++i) {
    const Token &t = tokens[i];
    if (t.kind == Tok::Eof)
      break;
    if (t.line > line1 || (t.line == line1 && t.col > col1))
      break;
    if (t.kind == Tok::Identifier) {
      pending = t.text;
      pendingQual = "";
      if (i >= 2 && tokens[i - 1].kind == Tok::Dot &&
          tokens[i - 2].kind == Tok::Identifier)
        pendingQual = tokens[i - 2].text;
    } else if (t.kind == Tok::LParen) {
      stack.push_back({pending, pendingQual, 0});
      pending.clear();
      pendingQual.clear();
    } else if (t.kind == Tok::RParen) {
      if (!stack.empty())
        stack.pop_back();
    } else if (t.kind == Tok::Comma && !stack.empty()) {
      stack.back().param++;
    }
  }
  if (stack.empty() || stack.back().name.empty())
    return false;
  out.name = stack.back().name;
  out.qualifier = stack.back().qualifier;
  out.activeParam = stack.back().param;
  return true;
}

Json locationPayload(const NavSymbol &s);

struct IdentHit {
  int line = 0;
  int col = 0;
  int endCol = 0;
  std::string qualifier;
  bool isWrite = false;
};

bool isDeclTok(Tok k) {
  return k == Tok::Function || k == Tok::Struct || k == Tok::Data ||
         k == Tok::Class || k == Tok::Trait || k == Tok::Enum ||
         k == Tok::Module || k == Tok::Variable || k == Tok::Constant ||
         k == Tok::Signal;
}

bool isAssignTok(Tok k) {
  return k == Tok::Eq || k == Tok::PlusEq || k == Tok::MinusEq ||
         k == Tok::StarEq || k == Tok::SlashEq;
}

std::vector<IdentHit> identHits(const std::string &source,
                                const std::string &name) {
  std::vector<IdentHit> out;
  std::vector<Token> tokens;
  try {
    tokens = tokenize(source, "");
  } catch (...) {
    return out;
  }
  for (size_t i = 0; i < tokens.size(); ++i) {
    const Token &t = tokens[i];
    if (t.kind != Tok::Identifier || t.text != name)
      continue;
    IdentHit h;
    h.line = t.line > 0 ? t.line - 1 : 0;
    h.col = t.col > 0 ? t.col - 1 : 0;
    h.endCol = h.col + static_cast<int>(t.text.size());
    if (i >= 2 && tokens[i - 1].kind == Tok::Dot &&
        tokens[i - 2].kind == Tok::Identifier)
      h.qualifier = tokens[i - 2].text;
    if (i > 0 && isDeclTok(tokens[i - 1].kind))
      h.isWrite = true;
    if (i + 1 < tokens.size() && isAssignTok(tokens[i + 1].kind))
      h.isWrite = true;
    out.push_back(std::move(h));
  }
  return out;
}

bool hitMatchesCursor(const IdentAt &ident, const IdentHit &h) {
  if (!ident.qualifier.empty())
    return h.qualifier == ident.qualifier || h.isWrite;
  return true;
}

bool posInSpan(int line, int col, int sl, int sc, int el, int ec);

Json identLocations(const std::string &source, const std::string &uri,
                    const std::string &name) {
  Json arr = Json::array();
  for (const auto &h : identHits(source, name)) {
    NavSymbol s;
    s.name = name;
    s.uri = uri;
    s.line = h.line;
    s.col = h.col;
    arr.a.push_back(locationPayload(s));
  }
  return arr;
}

std::string lineEnding(const std::string &source) {
  return source.find("\r\n") != std::string::npos ? "\r\n" : "\n";
}

std::string lineIndent(const std::string &source, int line0) {
  auto lines = splitLines(source);
  if (line0 < 0 || line0 >= static_cast<int>(lines.size()))
    return "    ";
  const std::string &line = lines[static_cast<size_t>(line0)];
  size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
    ++i;
  if (i == 0)
    return "    ";
  return line.substr(0, i);
}

std::vector<std::string> quotedIdents(const std::string &s) {
  std::vector<std::string> out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '\'')
      continue;
    size_t j = i + 1;
    while (j < s.size() && s[j] != '\'')
      ++j;
    if (j >= s.size())
      break;
    out.push_back(s.substr(i + 1, j - i - 1));
    i = j;
  }
  return out;
}

int importInsertLine(const std::string &text) {
  auto lines = splitLines(text);
  int last = -1;
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    const std::string t = trimCopy(lines[static_cast<size_t>(i)]);
    if (t.rfind("import ", 0) == 0 || t.rfind("from ", 0) == 0)
      last = i;
  }
  return last + 1;
}

bool fileHasCrateImport(const std::string &text, const std::string &crate) {
  auto lines = splitLines(text);
  for (const auto &line : lines) {
    const std::string t = trimCopy(line);
    if (t == "import " + crate + ";" || t == "import std." + crate + ";")
      return true;
    if (crate == "std" && t.rfind("import std", 0) == 0)
      return true;
  }
  return false;
}

std::string crateFromDiag(const std::string &msg) {
  const std::string p = "(in crate ";
  const size_t a = msg.find(p);
  if (a == std::string::npos)
    return "";
  const size_t start = a + p.size();
  const size_t b = msg.find_first_of(";)", start);
  if (b == std::string::npos)
    return "";
  std::string c = msg.substr(start, b - start);
  const size_t orPos = c.find(" or ");
  if (orPos != std::string::npos)
    c = c.substr(0, orPos);
  return trimCopy(c);
}

bool isIdentName(const std::string &s) {
  if (s.empty())
    return false;
  const unsigned char c = static_cast<unsigned char>(s[0]);
  if (!(std::isalpha(c) || s[0] == '_'))
    return false;
  for (size_t i = 1; i < s.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(s[i]);
    if (!(std::isalnum(ch) || s[i] == '_'))
      return false;
  }
  return true;
}

void leftmostExpr(const Expr &e, int &line, int &col) {
  if (!e.kids.empty() &&
      (e.kind == Expr::Kind::MethodCall || e.kind == Expr::Kind::Member ||
       e.kind == Expr::Kind::Index)) {
    leftmostExpr(e.kids[0], line, col);
    return;
  }
  line = e.line;
  col = e.col;
}

bool findCallAt(const Expr &e, int line, int col, Expr &out) {
  if ((e.kind == Expr::Kind::Call || e.kind == Expr::Kind::MethodCall) &&
      e.line == line && e.col == col) {
    out = e;
    return true;
  }
  for (const auto &k : e.kids) {
    if (findCallAt(k, line, col, out))
      return true;
  }
  return false;
}

bool findCallInStmts(const std::vector<Stmt> &stmts, int line, int col,
                     Expr &out);

bool findCallInFns(const std::vector<FnDecl> &fns, int line, int col,
                   Expr &out) {
  for (const auto &fn : fns) {
    if (findCallInStmts(fn.body, line, col, out))
      return true;
  }
  return false;
}

bool findCallInStmts(const std::vector<Stmt> &stmts, int line, int col,
                     Expr &out) {
  for (const auto &s : stmts) {
    if (findCallAt(s.expr, line, col, out) ||
        findCallAt(s.target, line, col, out))
      return true;
    if (findCallInStmts(s.body, line, col, out) ||
        findCallInStmts(s.elseBody, line, col, out))
      return true;
    for (const auto &arm : s.arms) {
      if (findCallInStmts(arm.body, line, col, out))
        return true;
    }
  }
  return false;
}

bool findCallInMod(const ModDecl &m, int line, int col, Expr &out) {
  if (findCallInFns(m.fns, line, col, out))
    return true;
  for (const auto &st : m.structs) {
    if (findCallInFns(st.methods, line, col, out))
      return true;
  }
  for (const auto &c : m.classes) {
    if (findCallInFns(c.methods, line, col, out))
      return true;
    for (const auto &ti : c.traitImpls) {
      if (findCallInFns(ti.methods, line, col, out))
        return true;
    }
  }
  for (const auto &im : m.impls) {
    if (findCallInFns(im.methods, line, col, out))
      return true;
  }
  for (const auto &nested : m.mods) {
    if (findCallInMod(nested, line, col, out))
      return true;
  }
  return false;
}

bool findCallInProgram(const Program &p, int line, int col, Expr &out) {
  if (findCallInFns(p.fns, line, col, out))
    return true;
  for (const auto &st : p.structs) {
    if (findCallInFns(st.methods, line, col, out))
      return true;
  }
  for (const auto &c : p.classes) {
    if (findCallInFns(c.methods, line, col, out))
      return true;
    for (const auto &ti : c.traitImpls) {
      if (findCallInFns(ti.methods, line, col, out))
        return true;
    }
  }
  for (const auto &im : p.impls) {
    if (findCallInFns(im.methods, line, col, out))
      return true;
  }
  for (const auto &m : p.mods) {
    if (findCallInMod(m, line, col, out))
      return true;
  }
  return false;
}

const TraitDecl *findTraitInMod(const ModDecl &m, const std::string &name) {
  for (const auto &t : m.traits) {
    if (t.name == name)
      return &t;
  }
  for (const auto &nested : m.mods) {
    if (const TraitDecl *hit = findTraitInMod(nested, name))
      return hit;
  }
  return nullptr;
}

const TraitDecl *findTraitInProgram(const Program &p, const std::string &name) {
  const std::string h = typeHead(name);
  for (const auto &t : p.traits) {
    if (t.name == h)
      return &t;
  }
  for (const auto &m : p.mods) {
    if (const TraitDecl *hit = findTraitInMod(m, h))
      return hit;
  }
  return nullptr;
}

const EnumDecl *findEnumInMod(const ModDecl &m, const std::string &name) {
  for (const auto &e : m.enums) {
    if (e.name == name)
      return &e;
  }
  for (const auto &nested : m.mods) {
    if (const EnumDecl *hit = findEnumInMod(nested, name))
      return hit;
  }
  return nullptr;
}

const EnumDecl *findEnumInProgram(const Program &p, const std::string &name) {
  for (const auto &e : p.enums) {
    if (e.name == name)
      return &e;
  }
  for (const auto &m : p.mods) {
    if (const EnumDecl *hit = findEnumInMod(m, name))
      return hit;
  }
  return nullptr;
}

std::string variantArmText(const EnumVariant &v) {
  if (v.arity <= 0)
    return v.name + " { pass; }";
  std::string s = v.name + "(";
  for (int i = 0; i < v.arity; ++i) {
    if (i)
      s += ", ";
    std::string bind = "p" + std::to_string(i);
    if (i < static_cast<int>(v.fieldNames.size()) && !v.fieldNames[static_cast<size_t>(i)].empty()) {
      s += v.fieldNames[static_cast<size_t>(i)] + ": ";
      bind = v.fieldNames[static_cast<size_t>(i)];
    }
    s += bind;
  }
  s += ") { pass; }";
  return s;
}

std::string defaultTraitBody(const TraitMethod &m) {
  if (m.throws)
    return "throw \"todo\";";
  const std::string &rt = m.returnType;
  if (rt.empty() || rt == "Void")
    return "pass;";
  if (rt == "String" || rt == "Str")
    return "return \"\";";
  if (rt == "Int")
    return "return 0;";
  if (rt == "Float")
    return "return 0.0;";
  if (rt == "Bool")
    return "return false;";
  if (rt == "Array" || (rt.size() > 6 && rt.compare(0, 6, "Array[") == 0))
    return "return [];";
  if (rt == "Map" || (rt.size() > 4 && rt.compare(0, 4, "Map[") == 0))
    return "return {};";
  return "pass;";
}

std::string stubTraitMethod(const TraitMethod &m, const std::string &indent,
                            const std::string &nl) {
  std::string s = indent + "fn " + m.name + "(";
  for (size_t i = 0; i < m.params.size(); ++i) {
    if (i)
      s += ", ";
    s += m.params[i];
    if (i < m.paramTypes.size() && !m.paramTypes[i].empty() &&
        m.params[i] != "self")
      s += ": " + m.paramTypes[i];
  }
  s += ")";
  if (m.throws)
    s += " throws";
  if (!m.returnType.empty())
    s += ": " + m.returnType;
  s += " {" + nl;
  s += indent + "    " + defaultTraitBody(m) + nl;
  s += indent + "}" + nl;
  return s;
}

bool tokenizeOk(const std::string &source, std::vector<Token> &tokens) {
  try {
    tokens = tokenize(source, "");
    return true;
  } catch (...) {
    return false;
  }
}

bool findKeywordClose(const std::string &source, Tok kind, int line1, int col1,
                      int &el, int &ec) {
  std::vector<Token> tokens;
  if (!tokenizeOk(source, tokens))
    return false;
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i].kind != kind)
      continue;
    if (tokens[i].line != line1)
      continue;
    if (col1 > 0 && tokens[i].col != col1)
      continue;
    return spanToClose(tokens, i, el, ec);
  }
  if (col1 > 0) {
    for (size_t i = 0; i < tokens.size(); ++i) {
      if (tokens[i].kind == kind && tokens[i].line == line1)
        return spanToClose(tokens, i, el, ec);
    }
  }
  return false;
}

bool findTypeBodyClose(const std::string &source, const std::string &typeName,
                       int &el, int &ec) {
  std::vector<Token> tokens;
  if (!tokenizeOk(source, tokens))
    return false;
  const std::string head = typeHead(typeName);
  for (size_t i = 0; i + 1 < tokens.size(); ++i) {
    const Tok k = tokens[i].kind;
    if (k != Tok::Class && k != Tok::Struct && k != Tok::Data &&
        k != Tok::Trait)
      continue;
    if (tokens[i + 1].kind == Tok::Identifier && tokens[i + 1].text == head)
      return spanToClose(tokens, i, el, ec);
  }
  return false;
}

bool findImplForClose(const std::string &source, const std::string &traitName,
                      const std::string &typeName, int &el, int &ec) {
  std::vector<Token> tokens;
  if (!tokenizeOk(source, tokens))
    return false;
  const std::string trait = typeHead(traitName);
  const std::string type = typeHead(typeName);
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i].kind != Tok::Implements)
      continue;
    std::string seenTrait;
    std::string seenType;
    bool sawFor = false;
    size_t j = i + 1;
    while (j < tokens.size() && tokens[j].kind != Tok::LBrace &&
           tokens[j].kind != Tok::Semi && tokens[j].kind != Tok::Eof) {
      if (tokens[j].kind == Tok::For)
        sawFor = true;
      else if (tokens[j].kind == Tok::Identifier) {
        if (sawFor)
          seenType = tokens[j].text;
        else if (seenTrait.empty())
          seenTrait = tokens[j].text;
      }
      ++j;
    }
    if (sawFor && seenTrait == trait && seenType == type)
      return spanToClose(tokens, i, el, ec);
  }
  return false;
}

bool typeBodyContains(const std::string &source, const std::string &typeName,
                      int line0, int col0) {
  std::vector<Token> tokens;
  if (!tokenizeOk(source, tokens))
    return false;
  const std::string head = typeHead(typeName);
  for (size_t i = 0; i + 1 < tokens.size(); ++i) {
    const Tok k = tokens[i].kind;
    if (k != Tok::Class && k != Tok::Struct && k != Tok::Data &&
        k != Tok::Trait)
      continue;
    if (tokens[i + 1].kind != Tok::Identifier || tokens[i + 1].text != head)
      continue;
    int el = 0, ec = 0;
    if (!spanToClose(tokens, i, el, ec))
      continue;
    const int sl = tokens[i].line > 0 ? tokens[i].line - 1 : 0;
    const int sc = tokens[i].col > 0 ? tokens[i].col - 1 : 0;
    if (posInSpan(line0, col0, sl, sc, el, ec))
      return true;
  }
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i].kind != Tok::Implements)
      continue;
    std::string seenType;
    bool sawFor = false;
    size_t j = i + 1;
    while (j < tokens.size() && tokens[j].kind != Tok::LBrace &&
           tokens[j].kind != Tok::Semi && tokens[j].kind != Tok::Eof) {
      if (tokens[j].kind == Tok::For)
        sawFor = true;
      else if (tokens[j].kind == Tok::Identifier && sawFor)
        seenType = tokens[j].text;
      ++j;
    }
    if (!sawFor || seenType != head)
      continue;
    int el = 0, ec = 0;
    if (!spanToClose(tokens, i, el, ec))
      continue;
    const int sl = tokens[i].line > 0 ? tokens[i].line - 1 : 0;
    const int sc = tokens[i].col > 0 ? tokens[i].col - 1 : 0;
    if (posInSpan(line0, col0, sl, sc, el, ec))
      return true;
  }
  return false;
}

bool matchBodyContains(const std::string &source, int matchLine1, int matchCol1,
                       int line0, int col0) {
  std::vector<Token> tokens;
  if (!tokenizeOk(source, tokens))
    return false;
  for (size_t i = 0; i < tokens.size(); ++i) {
    if (tokens[i].kind != Tok::Match)
      continue;
    if (matchLine1 > 0 && tokens[i].line != matchLine1)
      continue;
    int el = 0, ec = 0;
    if (!spanToClose(tokens, i, el, ec))
      continue;
    const int sl = tokens[i].line > 0 ? tokens[i].line - 1 : 0;
    const int sc = tokens[i].col > 0 ? tokens[i].col - 1 : 0;
    if (posInSpan(line0, col0, sl, sc, el, ec))
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

Json textEditJson(int sl, int sc, int el, int ec, const std::string &text) {
  Json edit = Json::object();
  edit.set("range", lspRange(sl, sc, el, ec));
  edit.set("newText", Json::str(text));
  return edit;
}

Json workspaceEditJson(const std::string &uri, Json edits) {
  Json changes = Json::object();
  changes.set(uri, std::move(edits));
  Json edit = Json::object();
  edit.set("changes", std::move(changes));
  return edit;
}

Json codeActionJson(const std::string &title, const std::string &kind,
                    Json edit, bool preferred) {
  Json a = Json::object();
  a.set("title", Json::str(title));
  a.set("kind", Json::str(kind));
  a.set("edit", std::move(edit));
  if (preferred)
    a.set("isPreferred", Json::boolean(true));
  return a;
}

bool rangesOverlap(int asl, int asc, int ael, int aec, int bsl, int bsc,
                   int bel, int bec) {
  if (ael < bsl || bel < asl)
    return false;
  if (ael == bsl && aec < bsc)
    return false;
  if (bel == asl && bec < asc)
    return false;
  return true;
}

bool posInSpan(int line, int col, int sl, int sc, int el, int ec) {
  if (line < sl || line > el)
    return false;
  if (line == sl && col < sc)
    return false;
  if (line == el && col > ec)
    return false;
  return true;
}

Json workspaceEditFromHits(
    const std::map<std::string, std::vector<IdentHit>> &byUri,
    const std::string &newName) {
  Json changes = Json::object();
  for (const auto &kv : byUri) {
    Json edits = Json::array();
    for (const auto &h : kv.second)
      edits.a.push_back(
          textEditJson(h.line, h.col, h.line, h.endCol, newName));
    if (!edits.a.empty())
      changes.set(kv.first, std::move(edits));
  }
  Json edit = Json::object();
  edit.set("changes", std::move(changes));
  return edit;
}

bool skipWalkDir(const std::string &name) {
  return name == "build" || name == ".git" || name == "node_modules" ||
         name == ".cursor";
}

std::string normPath(std::string p) {
#ifdef _WIN32
  for (char &c : p)
    if (c == '/')
      c = '\\';
  for (char &c : p)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
  return p;
}

bool pathHasBuiltin(const std::string &path) {
  std::string n = path;
  for (char &c : n)
    if (c == '\\')
      c = '/';
  return n.find("/builtin/") != std::string::npos ||
         n.rfind("builtin/", 0) == 0;
}

std::string stdlibCrateFromUri(const std::string &uri) {
  std::string n = uriToPath(uri);
  for (char &c : n) {
    if (c == '\\')
      c = '/';
  }
  const std::string key = "/builtin/std/";
  std::string rest;
  const size_t p = n.find(key);
  if (p != std::string::npos)
    rest = n.substr(p + key.size());
  else if (n.rfind("builtin/std/", 0) == 0)
    rest = n.substr(std::string("builtin/std/").size());
  else
    return "";
  const size_t slash = rest.find('/');
  if (slash == std::string::npos)
    return "std";
  return rest.substr(0, slash);
}

Json hoverPayload(const NavSymbol &s, const IdentAt *ident) {
  std::string sig = s.detail.empty() ? s.kind + " " + s.name : s.detail;
  std::string md = "```rosegold\n";
  if (s.deprecated)
    md += "@deprecated\n";
  md += sig;
  if (s.throws && sig.find("throws") == std::string::npos)
    md += " throws";
  md += "\n```";
  if (!s.doc.empty())
    md += "\n\n" + s.doc;
  const std::string crate = stdlibCrateFromUri(s.uri);
  if (!crate.empty() && s.doc.find("in crate") == std::string::npos)
    md += "\n\nin crate `" + crate + "`";
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
                        "Array", "Map",  "Range",  "UUID", "Vec2", "Vec3",
                        "Window"};

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

bool isKeywordName(const std::string &s) {
  for (const char *kw : kKeywords)
    if (s == kw)
      return true;
  return false;
}

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
  caps.set("referencesProvider", Json::boolean(true));
  Json signature = Json::object();
  Json sigTriggers = Json::array();
  sigTriggers.a.push_back(Json::str("("));
  sigTriggers.a.push_back(Json::str(","));
  signature.set("triggerCharacters", std::move(sigTriggers));
  caps.set("signatureHelpProvider", std::move(signature));
  Json completion = Json::object();
  Json triggers = Json::array();
  triggers.a.push_back(Json::str("."));
  triggers.a.push_back(Json::str("@"));
  triggers.a.push_back(Json::str(":"));
  completion.set("triggerCharacters", std::move(triggers));
  caps.set("completionProvider", std::move(completion));
  caps.set("documentSymbolProvider", Json::boolean(true));
  caps.set("documentHighlightProvider", Json::boolean(true));
  Json rename = Json::object();
  rename.set("prepareProvider", Json::boolean(true));
  caps.set("renameProvider", std::move(rename));
  Json codeAction = Json::object();
  Json actionKinds = Json::array();
  actionKinds.a.push_back(Json::str("quickfix"));
  codeAction.set("codeActionKinds", std::move(actionKinds));
  caps.set("codeActionProvider", std::move(codeAction));
  caps.set("codeLensProvider", Json::boolean(true));
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

  void forEachRg(
      const std::function<void(const std::string &uri, const std::string &text)>
          &fn,
      bool skipBuiltin) const {
    std::set<std::string> seen;
    for (const auto &kv : docs) {
      if (skipBuiltin && pathHasBuiltin(uriToPath(kv.first)))
        continue;
      fn(kv.first, kv.second);
      seen.insert(normPath(uriToPath(kv.first)));
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
      if (it->is_directory(ec) && skipWalkDir(name)) {
        it.disable_recursion_pending();
        continue;
      }
      if (!it->is_regular_file(ec) || it->path().extension() != ".rg")
        continue;
      const std::string path = it->path().string();
      if (skipBuiltin && pathHasBuiltin(path))
        continue;
      if (seen.count(normPath(path)))
        continue;
      try {
        fn(pathToUri(path), readFile(path));
      } catch (...) {
      }
    }
  }

  std::map<std::string, std::vector<IdentHit>>
  collectRefHits(const IdentAt &ident, bool skipBuiltin) const {
    std::map<std::string, std::vector<IdentHit>> out;
    forEachRg(
        [&](const std::string &uri, const std::string &text) {
          std::vector<IdentHit> hits;
          for (const auto &h : identHits(text, ident.name)) {
            if (hitMatchesCursor(ident, h))
              hits.push_back(h);
          }
          if (!hits.empty())
            out[uri] = std::move(hits);
        },
        skipBuiltin);
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
    if (ident.qualifier == "io" || ident.qualifier == "time" ||
        ident.qualifier == "path" || ident.qualifier == "math" ||
        ident.qualifier == "str" || ident.qualifier == "json" ||
        ident.qualifier == "ui" || ident.qualifier == "vec" ||
        ident.qualifier == "std") {
      NavSymbol s;
      s.name = ident.name;
      s.kind = ident.name == "Window" || ident.name == "Vec2" ||
                       ident.name == "Vec3" || ident.name == "UUID"
                   ? "class"
                   : "fn";
      s.detail = ident.qualifier + "." + ident.name;
      if (ident.name == "read_text" || ident.name == "read_lines" ||
          ident.name == "write_text" || ident.name == "parse" ||
          ident.name == "open" || ident.name == "open_hidden")
        s.throws = true;
      if (ident.name == "read_text" || ident.name == "read_lines" ||
          ident.name == "write_text" || ident.name == "parse" ||
          ident.name == "open" || ident.name == "open_hidden")
        s.detail += " throws";
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
    if (!hit) {
      if (const StdlibExport *ex =
              lookupStdlibExport(ident.name, uriToPath(uri))) {
        NavSymbol s;
        s.name = ident.name;
        s.kind = ex->kind;
        s.detail = std::string(ex->kind) + " " + ident.name;
        s.doc = "in crate `" + ex->crate + "`. Try `import " + ex->crate + "`.";
        return hoverPayload(s, &ident);
      }
      return Json::null();
    }
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

  Json signatureHelp(const Json &params) {
    const Json *td = params.getObj("textDocument");
    const Json *position = params.getObj("position");
    if (!td || !position)
      return Json::null();
    const std::string uri = td->getStr("uri");
    const std::string text = docText(uri);
    CallSite site;
    if (!callSiteAt(text, jsonInt(position, "line"),
                    jsonInt(position, "character"), site))
      return Json::null();
    const NavSymbol *hit = nullptr;
    for (const auto &s : symbols()) {
      if (s.name != site.name || s.kind != "fn")
        continue;
      if (!site.qualifier.empty() &&
          s.detail.find(site.qualifier) == std::string::npos &&
          s.uri.find(site.qualifier) == std::string::npos)
        continue;
      hit = &s;
      if (s.uri == uri)
        break;
    }
    std::string label = site.name + "(";
    Json parr = Json::array();
    std::vector<std::string> ps;
    bool throws = false;
    std::string doc;
    if (hit) {
      ps = hit->params;
      throws = hit->throws;
      doc = hit->doc;
    } else if (site.name == "print") {
      ps = {"..."};
    } else if (site.name == "len") {
      ps = {"xs"};
    } else if (site.name == "assert") {
      ps = {"cond"};
    }
    for (size_t i = 0; i < ps.size(); ++i) {
      if (i)
        label += ", ";
      label += ps[i];
      Json p = Json::object();
      p.set("label", Json::str(ps[i]));
      parr.a.push_back(std::move(p));
    }
    label += ")";
    if (throws)
      label += " throws";
    Json sig = Json::object();
    sig.set("label", Json::str(label));
    sig.set("parameters", std::move(parr));
    if (!doc.empty()) {
      Json d = Json::object();
      d.set("kind", Json::str("markdown"));
      d.set("value", Json::str(doc));
      sig.set("documentation", std::move(d));
    }
    Json sigs = Json::array();
    sigs.a.push_back(std::move(sig));
    Json help = Json::object();
    help.set("signatures", std::move(sigs));
    help.set("activeSignature", Json::num(0));
    int active = site.activeParam;
    if (active < 0)
      active = 0;
    if (!ps.empty() && active >= static_cast<int>(ps.size()))
      active = static_cast<int>(ps.size()) - 1;
    help.set("activeParameter", Json::num(active));
    return help;
  }

  Json references(const Json &params) {
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
    for (const auto &kv : collectRefHits(ident, false)) {
      for (const auto &h : kv.second) {
        NavSymbol s;
        s.name = ident.name;
        s.uri = kv.first;
        s.line = h.line;
        s.col = h.col;
        arr.a.push_back(locationPayload(s));
      }
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
            completionItem("time", 9, "std.time", "now / sleep", "time"));
        items.a.push_back(
            completionItem("path", 9, "std.path", "join / parent / stem",
                           "path"));
        items.a.push_back(
            completionItem("json", 9, "std.json", "parse / stringify", "json"));
        items.a.push_back(completionItem(
            "ui", 9, "std.ui", "Native windows (open / run)", "ui"));
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
      } else if (recv == "time") {
        items.a.push_back(
            completionItem("now", 2, "time.now()", "Unix epoch milliseconds",
                           "now()"));
        items.a.push_back(completionItem(
            "sleep", 2, "time.sleep(ms)", "Sleep milliseconds", "sleep($0)"));
      } else if (recv == "path") {
        items.a.push_back(completionItem(
            "join", 2, "path.join(a, b)", "Join path segments", "join($1, $2)"));
        items.a.push_back(completionItem(
            "parent", 2, "path.parent(p)", "Parent directory", "parent($0)"));
        items.a.push_back(
            completionItem("stem", 2, "path.stem(p)", "Filename without extension",
                           "stem($0)"));
      } else if (recv == "json") {
        items.a.push_back(completionItem(
            "parse", 2, "json.parse(s) throws", "Parse JSON to Map/Array/value",
            "parse($0)"));
        items.a.push_back(completionItem(
            "stringify", 2, "json.stringify(v)", "Encode a value as JSON",
            "stringify($0)"));
        items.a.push_back(completionItem(
            "valid", 2, "json.valid(s)", "True if s is JSON this crate can parse",
            "valid($0)"));
      } else if (recv == "vec") {
        items.a.push_back(
            completionItem("Vec2", 7, "class Vec2", "Vec2 { x, y }", "Vec2"));
        items.a.push_back(completionItem(
            "Vec3", 7, "class Vec3", "Vec3 { x, y, z } extends Vec2", "Vec3"));
      } else if (recv == "ui") {
        items.a.push_back(completionItem(
            "open", 3, "ui.open(title, width, height) throws",
            "Create a visible native window", "open($1, $2, $3)"));
        items.a.push_back(completionItem(
            "open_hidden", 3, "ui.open_hidden(title, width, height) throws",
            "Create a hidden window (tests)", "open_hidden($1, $2, $3)"));
        items.a.push_back(completionItem(
            "run", 3, "ui.run()", "Pump messages until every window closes",
            "run()"));
        items.a.push_back(
            completionItem("count", 3, "ui.count()", "Living windows",
                           "count()"));
        items.a.push_back(completionItem(
            "backend", 3, "ui.backend()", "win32, x11, wayland, or cocoa",
            "backend()"));
        items.a.push_back(completionItem(
            "font_height", 3, "ui.font_height()", "Pixel height of the UI font",
            "font_height()"));
        items.a.push_back(completionItem(
            "text_width", 3, "ui.text_width(s)", "Pixel width of a string",
            "text_width($0)"));
        items.a.push_back(completionItem(
            "rgb", 3, "ui.rgb(r, g, b)", "Custom Color.Rgb(r, g, b)",
            "rgb($1, $2, $3)"));
        items.a.push_back(completionItem(
            "Color", 13, "enum Color",
            "Black, Red, Green, Yellow, Blue, Magenta, Cyan, White, Rgb(r, g, b)",
            "Color"));
        items.a.push_back(completionItem(
            "padding", 3, "w.padding(n)", "Wrap a widget with inset",
            "padding($0)"));
        items.a.push_back(completionItem(
            "background", 3, "w.background(color)",
            "Paint a fill behind a widget", "background($0)"));
        items.a.push_back(completionItem(
            "style", 3, "w.style(Style { fill, pad })",
            "Fill and padding bag", "style($0)"));
        items.a.push_back(completionItem(
            "Window", 7, "class Window",
            "Window { title, width, height, visible }", "Window"));
        items.a.push_back(completionItem(
            "Label", 7, "class Label", "Label { text } implements Widget",
            "Label"));
        items.a.push_back(completionItem(
            "Button", 7, "class Button",
            "Button { text } with clicked signal", "Button"));
        items.a.push_back(completionItem(
            "VStack", 7, "class VStack",
            "VStack { spacing, children } vertical layout", "VStack"));
        items.a.push_back(completionItem(
            "HStack", 7, "class HStack",
            "HStack { spacing, children } horizontal layout", "HStack"));
        items.a.push_back(completionItem(
            "Style", 7, "class Style", "Style { fill, ink, pad }", "Style"));
        items.a.push_back(completionItem(
            "Widget", 8, "trait Widget",
            "height / paint / handle_click", "Widget"));
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
            items.a.push_back(completionItem(
                "emit_deferred", 2, recv + ".emit_deferred()",
                "Queue emit until the current function returns",
                fnSnippet("emit_deferred", s.params)));
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
      std::set<std::string> seen;
      const std::string from = uriToPath(uri);
      for (const char *t : kTypes) {
        std::string detail = "type";
        if (const StdlibExport *ex = lookupStdlibExport(t, from))
          detail = "crate " + ex->crate;
        items.a.push_back(completionItem(t, 25, detail, "", t));
        seen.insert(t);
      }
      for (const auto &kv : stdlibExportIndex(from)) {
        for (const auto &ex : kv.second) {
          if (ex.kind == "fn")
            continue;
          if (!seen.insert(kv.first).second)
            continue;
          items.a.push_back(completionItem(
              kv.first, 25, "crate " + ex.crate,
              ex.kind + " in " + ex.crate, kv.first));
        }
      }
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

  Json documentSymbols(const Json &params) {
    const Json *td = params.getObj("textDocument");
    if (!td)
      return Json::array();
    const std::string uri = td->getStr("uri");
    const std::string text = docText(uri);
    std::vector<NavSymbol> syms;
    collectFromSource(text, uri, syms);
    const int n = static_cast<int>(syms.size());
    std::vector<int> parent(static_cast<size_t>(n), -1);
    auto contains = [&](int outer, int inner) {
      if (outer == inner)
        return false;
      if (!canOutlineParent(syms[static_cast<size_t>(outer)].kind))
        return false;
      const NavSymbol &a = syms[static_cast<size_t>(outer)];
      const NavSymbol &b = syms[static_cast<size_t>(inner)];
      if (b.line < a.line || b.line > a.endLine)
        return false;
      if (b.line == a.line && b.col < a.col)
        return false;
      if (b.line == a.endLine && b.col > a.endCol)
        return false;
      return true;
    };
    for (int i = 0; i < n; ++i) {
      int best = -1;
      for (int j = 0; j < n; ++j) {
        if (!contains(j, i))
          continue;
        if (best < 0 || contains(best, j))
          best = j;
      }
      parent[static_cast<size_t>(i)] = best;
    }
    std::function<Json(int)> emit = [&](int i) -> Json {
      Json kids = Json::array();
      for (int c = 0; c < n; ++c) {
        if (parent[static_cast<size_t>(c)] == i)
          kids.a.push_back(emit(c));
      }
      const NavSymbol &s = syms[static_cast<size_t>(i)];
      Json o = Json::object();
      o.set("name", Json::str(s.name));
      o.set("detail", Json::str(s.kind));
      o.set("kind", Json::num(lspSymbolKind(s.kind)));
      o.set("range", lspRange(s.line, s.col, s.endLine, s.endCol));
      o.set("selectionRange",
            lspRange(s.line, s.col, s.line,
                     s.col + static_cast<int>(s.name.size())));
      if (!kids.a.empty())
        o.set("children", std::move(kids));
      if (s.deprecated) {
        Json tags = Json::array();
        tags.a.push_back(Json::num(1));
        o.set("tags", std::move(tags));
      }
      return o;
    };
    Json arr = Json::array();
    for (int i = 0; i < n; ++i) {
      if (parent[static_cast<size_t>(i)] < 0)
        arr.a.push_back(emit(i));
    }
    return arr;
  }

  Json documentHighlight(const Json &params) {
    const Json *td = params.getObj("textDocument");
    const Json *position = params.getObj("position");
    if (!td || !position)
      return Json::array();
    const std::string uri = td->getStr("uri");
    const std::string text = docText(uri);
    IdentAt ident;
    if (!identAt(text, jsonInt(position, "line"),
                 jsonInt(position, "character"), ident))
      return Json::array();
    Json arr = Json::array();
    for (const auto &h : identHits(text, ident.name)) {
      if (!hitMatchesCursor(ident, h))
        continue;
      Json item = Json::object();
      item.set("range", lspRange(h.line, h.col, h.line, h.endCol));
      item.set("kind", Json::num(h.isWrite ? 3 : 2));
      arr.a.push_back(std::move(item));
    }
    return arr;
  }

  Json prepareRename(const Json &params) {
    const Json *td = params.getObj("textDocument");
    const Json *position = params.getObj("position");
    if (!td || !position)
      return Json::null();
    const std::string text = docText(td->getStr("uri"));
    IdentAt ident;
    if (!identAt(text, jsonInt(position, "line"),
                 jsonInt(position, "character"), ident))
      return Json::null();
    if (isKeywordName(ident.name))
      return Json::null();
    return lspRange(ident.line, ident.col, ident.line, ident.endCol);
  }

  Json rename(const Json &params) {
    const Json *td = params.getObj("textDocument");
    const Json *position = params.getObj("position");
    if (!td || !position)
      return Json::null();
    const std::string newName = params.getStr("newName");
    if (!isIdentName(newName) || isKeywordName(newName))
      return Json::null();
    const std::string uri = td->getStr("uri");
    const std::string text = docText(uri);
    IdentAt ident;
    if (!identAt(text, jsonInt(position, "line"),
                 jsonInt(position, "character"), ident))
      return Json::null();
    if (ident.name == newName)
      return Json::null();
    if (isKeywordName(ident.name))
      return Json::null();
    const bool skipBuiltin = !pathHasBuiltin(uriToPath(uri));
    auto hits = collectRefHits(ident, skipBuiltin);
    if (hits.empty())
      return Json::null();
    return workspaceEditFromHits(hits, newName);
  }

  Json codeLens(const Json &params) {
    const Json *td = params.getObj("textDocument");
    if (!td)
      return Json::array();
    const std::string uri = td->getStr("uri");
    const std::string text = docText(uri);
    std::vector<NavSymbol> all;
    collectFromSource(text, uri, all);
    std::vector<NavSymbol> containers;
    for (const auto &s : all) {
      if (canOutlineParent(s.kind))
        containers.push_back(s);
    }
    auto nested = [&](const NavSymbol &s) {
      for (const auto &c : containers) {
        if (s.line < c.line || s.line > c.endLine)
          continue;
        if (s.line == c.line && s.col < c.col)
          continue;
        if (s.line == c.endLine && s.col > c.endCol)
          continue;
        if (s.line != c.line || s.col != c.col)
          return true;
      }
      return false;
    };
    Json arr = Json::array();
    for (const auto &s : all) {
      if (s.kind != "fn" || nested(s))
        continue;
      const bool runMain = s.name == "main";
      const bool runTest = s.isTest;
      if (!runMain && !runTest)
        continue;
      Json cmd = Json::object();
      cmd.set("title", Json::str(runTest ? "Run Test" : "Run"));
      cmd.set("command",
              Json::str(runTest ? "rosegoldc.testFile" : "rosegoldc.runFile"));
      Json args = Json::array();
      args.a.push_back(Json::str(uri));
      cmd.set("arguments", std::move(args));
      Json lens = Json::object();
      lens.set("range", lspRange(s.line, s.col, s.line,
                                 s.col + static_cast<int>(s.name.size())));
      lens.set("command", std::move(cmd));
      arr.a.push_back(std::move(lens));
    }
    return arr;
  }

  Json codeAction(const Json &params) {
    const Json *td = params.getObj("textDocument");
    const Json *range = params.getObj("range");
    if (!td)
      return Json::array();
    const std::string uri = td->getStr("uri");
    const std::string text = docText(uri);
    int sl = 0, sc = 0, el = 0, ec = 0;
    if (range) {
      const Json *start = range->getObj("start");
      const Json *end = range->getObj("end");
      sl = jsonInt(start, "line");
      sc = jsonInt(start, "character");
      el = jsonInt(end, "line", sl);
      ec = jsonInt(end, "character", sc);
    }
    auto diags = checkSource(text, uriToPath(uri));
    std::vector<Diagnostic> hit;
    for (const auto &d : diags) {
      int dsl = 0, dsc = 0, del = 0, dec = 0;
      tokenRange(text, d.line, d.col, dsl, dsc, del, dec);
      bool keep = rangesOverlap(dsl, dsc, del, dec, sl, sc, el, ec) ||
                  (d.line > 0 && d.line - 1 >= sl && d.line - 1 <= el);
      if (!keep && d.message.find(" is missing '") != std::string::npos &&
          d.message.find(" for trait '") != std::string::npos) {
        auto qs = quotedIdents(d.message);
        if (!qs.empty() && typeBodyContains(text, qs[0], sl, sc))
          keep = true;
      }
      if (!keep && d.message.find("match of ") != std::string::npos &&
          d.message.find("missing variant") != std::string::npos) {
        if (matchBodyContains(text, d.line, d.col, sl, sc))
          keep = true;
      }
      if (keep)
        hit.push_back(d);
    }
    Program program;
    {
      std::vector<Diagnostic> parseErrs;
      program = parseSource(text, uriToPath(uri), &parseErrs);
    }
    Json actions = Json::array();
    const std::string nl = lineEnding(text);

    for (const auto &d : hit) {
      if (d.message.find("requires 'try'") == std::string::npos)
        continue;
      Expr call;
      if (!findCallInProgram(program, d.line, d.col, call))
        continue;
      int line = call.line;
      int col = call.col;
      leftmostExpr(call, line, col);
      const int il = line > 0 ? line - 1 : 0;
      const int ic = col > 0 ? col - 1 : 0;
      Json edits = Json::array();
      edits.a.push_back(textEditJson(il, ic, il, ic, "try "));
      actions.a.push_back(codeActionJson("Wrap with try", "quickfix",
                                         workspaceEditJson(uri, std::move(edits)),
                                         true));
    }

    for (const auto &d : hit) {
      if (d.message.find("match of ") == std::string::npos ||
          d.message.find("missing variant") == std::string::npos)
        continue;
      const std::string prefix = "match of ";
      const size_t a = d.message.find(prefix);
      const size_t b = d.message.find(" is missing", a);
      if (a == std::string::npos || b == std::string::npos)
        continue;
      const std::string enumName =
          d.message.substr(a + prefix.size(), b - a - prefix.size());
      const EnumDecl *en = findEnumInProgram(program, enumName);
      auto missing = quotedIdents(d.message);
      if (missing.empty())
        continue;
      int closeL = 0, closeC = 0;
      if (!findKeywordClose(text, Tok::Match, d.line, d.col, closeL, closeC))
        continue;
      const std::string indent = lineIndent(text, closeL);
      const std::string inner = indent + "    ";
      std::string insert;
      for (const auto &name : missing) {
        EnumVariant var;
        var.name = name;
        if (en) {
          for (const auto &v : en->variants) {
            if (v.name == name) {
              var = v;
              break;
            }
          }
        }
        insert += inner + variantArmText(var) + nl;
      }
      Json edits = Json::array();
      edits.a.push_back(textEditJson(closeL, closeC, closeL, closeC, insert));
      actions.a.push_back(codeActionJson(
          "Add missing match arms", "quickfix",
          workspaceEditJson(uri, std::move(edits)), true));
    }

    struct MissingTrait {
      std::string typeName;
      std::string traitName;
      std::vector<std::string> methods;
    };
    std::vector<MissingTrait> missingTraits;
    auto addMissing = [&](const std::string &typeName,
                          const std::string &traitName,
                          const std::string &method) {
      for (auto &m : missingTraits) {
        if (m.typeName == typeName && m.traitName == traitName) {
          if (std::find(m.methods.begin(), m.methods.end(), method) ==
              m.methods.end())
            m.methods.push_back(method);
          return;
        }
      }
      missingTraits.push_back({typeName, traitName, {method}});
    };
    for (const auto &d : hit) {
      const std::string &msg = d.message;
      if (msg.find(" is missing '") == std::string::npos ||
          msg.find(" for trait '") == std::string::npos)
        continue;
      auto qs = quotedIdents(msg);
      if (qs.size() < 3)
        continue;
      addMissing(qs[0], qs[2], qs[1]);
    }
    for (const auto &m : missingTraits) {
      const TraitDecl *tr = findTraitInProgram(program, m.traitName);
      int closeL = 0, closeC = 0;
      if (!findImplForClose(text, m.traitName, m.typeName, closeL, closeC) &&
          !findTypeBodyClose(text, m.typeName, closeL, closeC))
        continue;
      const std::string indent = lineIndent(text, closeL) + "    ";
      auto methodOf = [&](const std::string &name) -> const TraitMethod * {
        if (!tr)
          return nullptr;
        for (const auto &tm : tr->methods) {
          if (tm.name == name)
            return &tm;
        }
        return nullptr;
      };
      auto stubOne = [&](const std::string &name) {
        if (const TraitMethod *tm = methodOf(name))
          return stubTraitMethod(*tm, indent, nl);
        TraitMethod fake;
        fake.name = name;
        fake.params = {"self"};
        return stubTraitMethod(fake, indent, nl);
      };
      if (m.methods.size() > 1) {
        std::string insert;
        for (const auto &name : m.methods)
          insert += stubOne(name);
        Json edits = Json::array();
        edits.a.push_back(
            textEditJson(closeL, closeC, closeL, closeC, insert));
        actions.a.push_back(codeActionJson(
            "Implement missing methods for " + m.traitName, "quickfix",
            workspaceEditJson(uri, std::move(edits)), true));
      }
      for (const auto &name : m.methods) {
        Json edits = Json::array();
        edits.a.push_back(textEditJson(closeL, closeC, closeL, closeC,
                                       stubOne(name)));
        actions.a.push_back(codeActionJson(
            "Add method '" + name + "'", "quickfix",
            workspaceEditJson(uri, std::move(edits)), false));
      }
    }

    {
      const std::string from = uriToPath(uri);
      std::set<std::string> crates;
      auto consider = [&](const std::string &crate) {
        if (crate.empty() || !crates.insert(crate).second)
          return;
        if (fileHasCrateImport(text, crate))
          return;
        const int il = importInsertLine(text);
        Json edits = Json::array();
        edits.a.push_back(
            textEditJson(il, 0, il, 0, "import " + crate + ";" + nl));
        actions.a.push_back(codeActionJson(
            "Import crate " + crate, "quickfix",
            workspaceEditJson(uri, std::move(edits)), true));
      };
      for (const auto &d : diags) {
        std::string crate;
        auto qs = quotedIdents(d.message);
        if (!qs.empty()) {
          if (const StdlibExport *ex = lookupStdlibExport(qs[0], from))
            crate = ex->crate;
        }
        if (crate.empty())
          crate = crateFromDiag(d.message);
        consider(crate);
      }
    }
    return actions;
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
    if (method == "textDocument/signatureHelp") {
      writeResponse(id, signatureHelp(p));
      return;
    }
    if (method == "textDocument/references") {
      writeResponse(id, references(p));
      return;
    }
    if (method == "textDocument/completion") {
      writeResponse(id, completion(p));
      return;
    }
    if (method == "textDocument/documentSymbol") {
      writeResponse(id, documentSymbols(p));
      return;
    }
    if (method == "textDocument/documentHighlight") {
      writeResponse(id, documentHighlight(p));
      return;
    }
    if (method == "textDocument/prepareRename") {
      writeResponse(id, prepareRename(p));
      return;
    }
    if (method == "textDocument/rename") {
      writeResponse(id, rename(p));
      return;
    }
    if (method == "textDocument/codeLens") {
      writeResponse(id, codeLens(p));
      return;
    }
    if (method == "textDocument/codeAction") {
      writeResponse(id, codeAction(p));
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
        syms[0].params.size() != 2 || syms[0].params[0] != "a: Int" ||
        syms[0].params[1] != "b: Int" ||
        syms[0].doc.find("add two") == std::string::npos)
      return false;
    IdentAt ident;
    if (!identAt("fn main(): Int { return add(1, 2); }\n", 0, 24, ident) ||
        ident.name != "add")
      return false;
    CallSite site;
    if (!callSiteAt("add(1, 2)", 0, 7, site) || site.name != "add" ||
        site.activeParam != 1)
      return false;
    std::vector<NavSymbol> boom;
    collectFromSource("@deprecated\nfn boom() throws {\n    throw \"x\";\n}\n",
                      "file:///boom.rg", boom);
    if (boom.size() != 1 || !boom[0].deprecated || !boom[0].throws)
      return false;
    Json locs = identLocations("fn add(): Int { return add(); }\n",
                               "file:///t.rg", "add");
    if (locs.a.size() < 2)
      return false;
    std::vector<NavSymbol> tests;
    collectFromSource("@test\nfn t() {\n}\nfn main(): Int {\n    return 0;\n}\n",
                      "file:///t.rg", tests);
    bool sawTest = false;
    bool sawMain = false;
    for (const auto &s : tests) {
      if (s.name == "t" && s.isTest)
        sawTest = true;
      if (s.name == "main" && s.kind == "fn")
        sawMain = true;
    }
    if (!sawTest || !sawMain)
      return false;
    if (quotedIdents("type 'Point' is missing 'label' for trait 'Named'")
            .size() != 3)
      return false;
    Server srv;
    srv.docs["file:///t.rg"] =
        "struct Point {\n    x: Int;\n    fn mag(): Int { return x; }\n}\nfn "
        "main(): Int { return 0; }\n";
    Json p = Json::object();
    Json td2 = Json::object();
    td2.set("uri", Json::str("file:///t.rg"));
    p.set("textDocument", std::move(td2));
    Json outline = srv.documentSymbols(p);
    if (outline.a.size() < 2)
      return false;
    Json lenses = srv.codeLens(p);
    bool hasRun = false;
    for (const auto &l : lenses.a) {
      const Json *cmd = l.getObj("command");
      if (cmd && cmd->getStr("title") == "Run")
        hasRun = true;
    }
    if (!hasRun)
      return false;
    srv.docs["file:///r.rg"] = "fn add(): Int { return add(); }\n";
    Json rp = Json::object();
    Json rtd = Json::object();
    rtd.set("uri", Json::str("file:///r.rg"));
    rp.set("textDocument", std::move(rtd));
    Json posj = Json::object();
    posj.set("line", Json::num(0));
    posj.set("character", Json::num(3));
    rp.set("position", posj);
    Json hl = srv.documentHighlight(rp);
    if (hl.a.size() < 2)
      return false;
    const Json *hk = hl.a[0].get("kind");
    if (!hk || !hk->integer || hk->i != 3)
      return false;
    rp.set("newName", Json::str("sum"));
    Json renamed = srv.rename(rp);
    if (!renamed.getObj("changes") || !renamed.getObj("changes")->get("file:///r.rg"))
      return false;
    srv.docs["file:///a.rg"] = "fn add(): Int { return 1; }\n";
    srv.docs["file:///b.rg"] = "fn main(): Int { return add(); }\n";
    Json xp = Json::object();
    Json xtd = Json::object();
    xtd.set("uri", Json::str("file:///a.rg"));
    xp.set("textDocument", std::move(xtd));
    Json xpos = Json::object();
    xpos.set("line", Json::num(0));
    xpos.set("character", Json::num(3));
    xp.set("position", xpos);
    xp.set("newName", Json::str("sum"));
    Json xren = srv.rename(xp);
    const Json *xch = xren.getObj("changes");
    if (!xch || !xch->get("file:///a.rg") || !xch->get("file:///b.rg"))
      return false;
    Json hrefs = srv.references(xp);
    if (hrefs.a.size() < 2)
      return false;
    srv.docs["file:///try.rg"] =
        "fn boom() throws {\n    throw \"x\";\n}\nfn main(): Int {\n    "
        "boom();\n    return 0;\n}\n";
    Json ap = Json::object();
    Json atd = Json::object();
    atd.set("uri", Json::str("file:///try.rg"));
    ap.set("textDocument", std::move(atd));
    Json ast = Json::object();
    ast.set("line", Json::num(4));
    ast.set("character", Json::num(4));
    Json ar = Json::object();
    ar.set("start", ast);
    ar.set("end", ast);
    ap.set("range", std::move(ar));
    Json acts = srv.codeAction(ap);
    bool wrap = false;
    for (const auto &a : acts.a) {
      if (a.getStr("title") == "Wrap with try")
        wrap = true;
    }
    if (!wrap)
      return false;
    srv.docs["file:///m.rg"] =
        "enum Color {\n    Red,\n    Green,\n}\nfn main(): Int {\n    var c = "
        "Color.Red;\n    match c {\n        Red { pass; }\n    }\n    return "
        "0;\n}\n";
    Json mp = Json::object();
    Json mtd = Json::object();
    mtd.set("uri", Json::str("file:///m.rg"));
    mp.set("textDocument", std::move(mtd));
    Json mst = Json::object();
    mst.set("line", Json::num(6));
    mst.set("character", Json::num(4));
    Json mr = Json::object();
    mr.set("start", mst);
    mr.set("end", mst);
    mp.set("range", std::move(mr));
    Json macts = srv.codeAction(mp);
    bool fill = false;
    for (const auto &a : macts.a) {
      if (a.getStr("title") == "Add missing match arms")
        fill = true;
    }
    if (!fill)
      return false;
    srv.docs["file:///tr.rg"] =
        "trait Named {\n    fn label(self): String;\n}\nclass Point impl Named "
        "{\n    var x: Int = 0;\n}\nfn main(): Int {\n    return 0;\n}\n";
    Json tp = Json::object();
    Json ttd = Json::object();
    ttd.set("uri", Json::str("file:///tr.rg"));
    tp.set("textDocument", std::move(ttd));
    Json tst = Json::object();
    tst.set("line", Json::num(3));
    tst.set("character", Json::num(6));
    Json tr = Json::object();
    tr.set("start", tst);
    tr.set("end", tst);
    tp.set("range", std::move(tr));
    Json tacts = srv.codeAction(tp);
    bool stub = false;
    for (const auto &a : tacts.a) {
      if (a.getStr("title") == "Add method 'label'")
        stub = true;
    }
    if (!stub)
      return false;
    srv.docs["file:///kbob.rg"] =
        "class Block impl Identifiable {\n    var x: Int = 0;\n}\nfn main(): "
        "Int {\n    return 0;\n}\n";
    Json kp = Json::object();
    Json ktd = Json::object();
    ktd.set("uri", Json::str("file:///kbob.rg"));
    kp.set("textDocument", std::move(ktd));
    Json kst = Json::object();
    kst.set("line", Json::num(0));
    kst.set("character", Json::num(18));
    Json kr = Json::object();
    kr.set("start", kst);
    kr.set("end", kst);
    kp.set("range", std::move(kr));
    Json kacts = srv.codeAction(kp);
    bool importStd = false;
    for (const auto &a : kacts.a) {
      if (a.getStr("title") == "Import crate std")
        importStd = true;
    }
    if (!importStd)
      return false;
    Json khovp = Json::object();
    Json khtd = Json::object();
    khtd.set("uri", Json::str("file:///kbob.rg"));
    khovp.set("textDocument", std::move(khtd));
    Json khpos = Json::object();
    khpos.set("line", Json::num(0));
    khpos.set("character", Json::num(18));
    khovp.set("position", std::move(khpos));
    Json khov = srv.hover(khovp);
    const Json *kcontents = khov.getObj("contents");
    if (!kcontents ||
        kcontents->getStr("value").find("crate") == std::string::npos)
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
