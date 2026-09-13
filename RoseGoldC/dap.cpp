#include "eval.h"
#include "interp.h"
#include "parser.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

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

  void set(const std::string &k, Json v) {
    for (auto &kv : o) {
      if (kv.first == k) {
        kv.second = std::move(v);
        return;
      }
    }
    o.emplace_back(k, std::move(v));
  }
  const Json *get(const char *key) const {
    for (const auto &kv : o)
      if (kv.first == key)
        return &kv.second;
    return nullptr;
  }
  const Json *getObj(const char *key) const {
    const Json *j = get(key);
    return j && j->kind == Kind::Object ? j : nullptr;
  }
  const Json *getArr(const char *key) const {
    const Json *j = get(key);
    return j && j->kind == Kind::Array ? j : nullptr;
  }
  std::string getStr(const char *key) const {
    const Json *j = get(key);
    return j && j->kind == Kind::String ? j->s : "";
  }
  long long getInt(const char *key, long long def = 0) const {
    const Json *j = get(key);
    if (!j || j->kind != Kind::Number)
      return def;
    return j->integer ? j->i : static_cast<long long>(j->n);
  }
  bool getBool(const char *key, bool def = false) const {
    const Json *j = get(key);
    return j && j->kind == Kind::Bool ? j->b : def;
  }
};

std::string escapeJson(const std::string &s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    if (c == '\\' || c == '"') {
      o.push_back('\\');
      o.push_back(static_cast<char>(c));
    } else if (c == '\n')
      o += "\\n";
    else if (c == '\r')
      o += "\\r";
    else if (c == '\t')
      o += "\\t";
    else if (c < 32) {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "\\u%04x", c);
      o += buf;
    } else
      o.push_back(static_cast<char>(c));
  }
  o += "\"";
  return o;
}

std::string encodeJson(const Json &j) {
  switch (j.kind) {
  case Json::Kind::Null:
    return "null";
  case Json::Kind::Bool:
    return j.b ? "true" : "false";
  case Json::Kind::Number:
    if (j.integer)
      return std::to_string(j.i);
    {
      std::ostringstream ss;
      ss << j.n;
      return ss.str();
    }
  case Json::Kind::String:
    return escapeJson(j.s);
  case Json::Kind::Array: {
    std::string o = "[";
    for (size_t i = 0; i < j.a.size(); ++i) {
      if (i)
        o += ",";
      o += encodeJson(j.a[i]);
    }
    o += "]";
    return o;
  }
  case Json::Kind::Object: {
    std::string o = "{";
    for (size_t i = 0; i < j.o.size(); ++i) {
      if (i)
        o += ",";
      o += escapeJson(j.o[i].first);
      o += ":";
      o += encodeJson(j.o[i].second);
    }
    o += "}";
    return o;
  }
  }
  return "null";
}

struct ParseCtx {
  const std::string *s = nullptr;
  size_t i = 0;
  void skip() {
    while (i < s->size() &&
           std::isspace(static_cast<unsigned char>((*s)[i])))
      ++i;
  }
  char peek() { return i < s->size() ? (*s)[i] : '\0'; }
  char get() { return i < s->size() ? (*s)[i++] : '\0'; }
};

Json parseValue(ParseCtx &c);

Json parseString(ParseCtx &c) {
  c.get();
  std::string out;
  while (c.i < c.s->size()) {
    char ch = c.get();
    if (ch == '"')
      break;
    if (ch == '\\') {
      char e = c.get();
      if (e == 'n')
        out.push_back('\n');
      else if (e == 'r')
        out.push_back('\r');
      else if (e == 't')
        out.push_back('\t');
      else
        out.push_back(e);
    } else
      out.push_back(ch);
  }
  return Json::str(std::move(out));
}

Json parseNumber(ParseCtx &c) {
  size_t start = c.i;
  if (c.peek() == '-')
    c.get();
  while (std::isdigit(static_cast<unsigned char>(c.peek())))
    c.get();
  bool real = false;
  if (c.peek() == '.') {
    real = true;
    c.get();
    while (std::isdigit(static_cast<unsigned char>(c.peek())))
      c.get();
  }
  const std::string num = c.s->substr(start, c.i - start);
  if (real) {
    Json j;
    j.kind = Json::Kind::Number;
    j.n = std::strtod(num.c_str(), nullptr);
    return j;
  }
  return Json::num(std::strtoll(num.c_str(), nullptr, 10));
}

Json parseArray(ParseCtx &c) {
  c.get();
  Json arr = Json::array();
  c.skip();
  if (c.peek() == ']') {
    c.get();
    return arr;
  }
  while (true) {
    arr.a.push_back(parseValue(c));
    c.skip();
    if (c.peek() == ',') {
      c.get();
      c.skip();
      continue;
    }
    if (c.peek() == ']') {
      c.get();
      break;
    }
    break;
  }
  return arr;
}

Json parseObject(ParseCtx &c) {
  c.get();
  Json obj = Json::object();
  c.skip();
  if (c.peek() == '}') {
    c.get();
    return obj;
  }
  while (true) {
    c.skip();
    Json key = parseString(c);
    c.skip();
    if (c.peek() == ':')
      c.get();
    c.skip();
    obj.set(key.s, parseValue(c));
    c.skip();
    if (c.peek() == ',') {
      c.get();
      continue;
    }
    if (c.peek() == '}') {
      c.get();
      break;
    }
    break;
  }
  return obj;
}

Json parseValue(ParseCtx &c) {
  c.skip();
  char ch = c.peek();
  if (ch == '"')
    return parseString(c);
  if (ch == '{')
    return parseObject(c);
  if (ch == '[')
    return parseArray(c);
  if (ch == 't') {
    c.i += 4;
    return Json::boolean(true);
  }
  if (ch == 'f') {
    c.i += 5;
    return Json::boolean(false);
  }
  if (ch == 'n') {
    c.i += 4;
    return Json::null();
  }
  if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)))
    return parseNumber(c);
  return Json::null();
}

Json parseJson(const std::string &s) {
  ParseCtx c;
  c.s = &s;
  return parseValue(c);
}

void writeMessage(const std::string &body) {
  const std::string header =
      "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
  fwrite(header.data(), 1, header.size(), stdout);
  fwrite(body.data(), 1, body.size(), stdout);
  fflush(stdout);
}

bool readMessage(std::string &body) {
  std::string header;
  char prev = 0;
  while (true) {
    int ch = fgetc(stdin);
    if (ch == EOF)
      return false;
    header.push_back(static_cast<char>(ch));
    if (prev == '\n' && ch == '\n')
      break;
    if (prev == '\r' && ch == '\n' && header.size() >= 4 &&
        header[header.size() - 4] == '\r' &&
        header[header.size() - 3] == '\n')
      break;
    prev = static_cast<char>(ch);
  }
  size_t len = 0;
  const char *p = std::strstr(header.c_str(), "Content-Length:");
  if (!p)
    return false;
  len = static_cast<size_t>(std::strtoul(p + 15, nullptr, 10));
  body.assign(len, '\0');
  size_t got = 0;
  while (got < len) {
    size_t n = fread(&body[got], 1, len - got, stdin);
    if (n == 0)
      return false;
    got += n;
  }
  return true;
}

void writeResponseNamed(long long id, const std::string &command, Json result) {
  Json msg = Json::object();
  msg.set("seq", Json::num(0));
  msg.set("type", Json::str("response"));
  msg.set("request_seq", Json::num(id));
  msg.set("success", Json::boolean(true));
  msg.set("command", Json::str(command));
  msg.set("body", std::move(result));
  writeMessage(encodeJson(msg));
}

void writeEvent(const std::string &event, Json body) {
  Json msg = Json::object();
  msg.set("seq", Json::num(0));
  msg.set("type", Json::str("event"));
  msg.set("event", Json::str(event));
  msg.set("body", std::move(body));
  writeMessage(encodeJson(msg));
}

struct DapServer {
  Interpreter *interp = nullptr;
  bool paused = false;
  bool done = false;
  bool configured = false;
  bool launched = false;
  bool running = false;
  std::string program;
  std::vector<std::string> args;
  std::string cwd;
  bool stopOnEntry = true;
  long long nextVarRef = 1000;
  std::map<long long, std::vector<std::pair<std::string, Value>>> varTables;

  void sendStopped(const std::string &reason) {
    Json body = Json::object();
    body.set("reason", Json::str(reason));
    body.set("threadId", Json::num(1));
    body.set("allThreadsStopped", Json::boolean(true));
    writeEvent("stopped", std::move(body));
  }

  void pauseAndWait(const std::string &reason) {
    paused = true;
    sendStopped(reason);
    while (paused && !done)
      processOne(false);
  }

  void processOne(bool allowLaunch) {
    std::string body;
    if (!readMessage(body)) {
      done = true;
      paused = false;
      if (interp)
        interp->debug.abort = true;
      return;
    }
    Json msg = parseJson(body);
    if (msg.getStr("type") != "request")
      return;
    const std::string command = msg.getStr("command");
    const long long id = msg.getInt("seq");
    const Json *params = msg.getObj("arguments");
    Json empty = Json::object();
    const Json &p = params ? *params : empty;
    handle(command, id, p, allowLaunch);
  }

  void handle(const std::string &command, long long id, const Json &p,
              bool allowLaunch) {
    if (command == "initialize") {
      Json caps = Json::object();
      caps.set("supportsConfigurationDoneRequest", Json::boolean(true));
      caps.set("supportsSingleThreadExecutionRequests", Json::boolean(true));
      caps.set("supportsConditionalBreakpoints", Json::boolean(true));
      caps.set("supportsEvaluateForHovers", Json::boolean(true));
      writeResponseNamed(id, command, std::move(caps));
      writeEvent("initialized", Json::object());
      return;
    }
    if (command == "launch") {
      program = p.getStr("program");
      if (!program.empty()) {
        std::error_code ec;
        auto abs = std::filesystem::absolute(program, ec);
        if (!ec)
          program = abs.string();
      }
      stopOnEntry = p.getBool("stopOnEntry", true);
      cwd = p.getStr("cwd");
      args.clear();
      args.push_back(program);
      if (const Json *arr = p.getArr("args")) {
        for (const auto &a : arr->a)
          if (a.kind == Json::Kind::String)
            args.push_back(a.s);
      }
      launched = true;
      writeResponseNamed(id, command, Json::object());
      return;
    }
    if (command == "setBreakpoints") {
      const Json *source = p.getObj("source");
      std::string path = source ? source->getStr("path") : "";
      if (!path.empty()) {
        std::error_code ec;
        auto abs = std::filesystem::absolute(path, ec);
        if (!ec)
          path = abs.string();
      }
      const std::string norm = Interpreter::debugNormPath(path);
      std::vector<Interpreter::DebugBreakpoint> bps;
      Json breakpoints = Json::array();
      if (const Json *arr = p.getArr("breakpoints")) {
        for (const auto &bp : arr->a) {
          Interpreter::DebugBreakpoint db;
          db.line = static_cast<int>(bp.getInt("line"));
          db.condition = bp.getStr("condition");
          if (db.line > 0)
            bps.push_back(db);
          Json out = Json::object();
          out.set("verified", Json::boolean(db.line > 0));
          out.set("line", Json::num(db.line));
          if (!db.condition.empty())
            out.set("message", Json::str("condition: " + db.condition));
          breakpoints.a.push_back(std::move(out));
        }
      }
      if (interp)
        interp->debug.breakpoints[norm] = bps;
      else
        pendingBreaks[norm] = bps;
      Json body = Json::object();
      body.set("breakpoints", std::move(breakpoints));
      writeResponseNamed(id, command, std::move(body));
      return;
    }
    if (command == "configurationDone") {
      configured = true;
      writeResponseNamed(id, command, Json::object());
      if (allowLaunch && launched && !running)
        startProgram();
      return;
    }
    if (command == "threads") {
      Json th = Json::object();
      th.set("id", Json::num(1));
      th.set("name", Json::str("main"));
      Json arr = Json::array();
      arr.a.push_back(std::move(th));
      Json body = Json::object();
      body.set("threads", std::move(arr));
      writeResponseNamed(id, command, std::move(body));
      return;
    }
    if (command == "stackTrace") {
      Json frames = Json::array();
      if (interp) {
        for (size_t i = 0; i < interp->debug.stack.size(); ++i) {
          const size_t idx = interp->debug.stack.size() - 1 - i;
          const auto &fr = interp->debug.stack[idx];
          Json source = Json::object();
          source.set("name", Json::str(fr.path));
          source.set("path", Json::str(fr.path));
          Json frame = Json::object();
          frame.set("id", Json::num(static_cast<long long>(idx + 1)));
          frame.set("name", Json::str(fr.name));
          frame.set("line", Json::num(fr.line));
          frame.set("column", Json::num(fr.col));
          frame.set("source", std::move(source));
          frames.a.push_back(std::move(frame));
        }
      }
      Json body = Json::object();
      body.set("stackFrames", std::move(frames));
      body.set("totalFrames", Json::num(static_cast<long long>(frames.a.size())));
      writeResponseNamed(id, command, std::move(body));
      return;
    }
    if (command == "scopes") {
      const long long frameId = p.getInt("frameId", 1);
      Json scopes = Json::array();
      Json locals = Json::object();
      locals.set("name", Json::str("Locals"));
      locals.set("variablesReference", Json::num(frameId));
      locals.set("expensive", Json::boolean(false));
      scopes.a.push_back(std::move(locals));
      Json body = Json::object();
      body.set("scopes", std::move(scopes));
      writeResponseNamed(id, command, std::move(body));
      return;
    }
    if (command == "variables") {
      const long long ref = p.getInt("variablesReference");
      Json vars = Json::array();
      if (interp && ref >= 1 &&
          static_cast<size_t>(ref) <= interp->debug.stack.size()) {
        const auto &fr = interp->debug.stack[static_cast<size_t>(ref - 1)];
        if (fr.envIndex < interp->env.size()) {
          for (const auto &kv : interp->env[fr.envIndex]) {
            Json v = Json::object();
            v.set("name", Json::str(kv.first));
            v.set("value", Json::str(kv.second.value.toString()));
            v.set("type", Json::str("Value"));
            v.set("variablesReference", Json::num(0));
            vars.a.push_back(std::move(v));
          }
        }
      }
      Json body = Json::object();
      body.set("variables", std::move(vars));
      writeResponseNamed(id, command, std::move(body));
      return;
    }
    if (command == "continue") {
      if (interp)
        interp->debug.mode = Interpreter::DebugState::Mode::Run;
      paused = false;
      Json body = Json::object();
      body.set("allThreadsContinued", Json::boolean(true));
      writeResponseNamed(id, command, std::move(body));
      return;
    }
    if (command == "next") {
      if (interp) {
        interp->debug.mode = Interpreter::DebugState::Mode::Next;
        interp->debug.stepDepth = interp->debug.stack.size();
      }
      paused = false;
      writeResponseNamed(id, command, Json::object());
      return;
    }
    if (command == "stepIn") {
      if (interp)
        interp->debug.mode = Interpreter::DebugState::Mode::StepIn;
      paused = false;
      writeResponseNamed(id, command, Json::object());
      return;
    }
    if (command == "stepOut") {
      if (interp) {
        interp->debug.mode = Interpreter::DebugState::Mode::StepOut;
        interp->debug.stepDepth = interp->debug.stack.size();
      }
      paused = false;
      writeResponseNamed(id, command, Json::object());
      return;
    }
    if (command == "evaluate") {
      Json body = Json::object();
      if (!interp) {
        body.set("result", Json::str(""));
        body.set("variablesReference", Json::num(0));
        writeResponseNamed(id, command, std::move(body));
        return;
      }
      const std::string expression = p.getStr("expression");
      std::string err;
      Value v = interp->debugEval(expression, err);
      if (!err.empty()) {
        body.set("result", Json::str(err));
        body.set("variablesReference", Json::num(0));
      } else {
        body.set("result", Json::str(v.toString()));
        body.set("type", Json::str("Value"));
        body.set("variablesReference", Json::num(0));
      }
      writeResponseNamed(id, command, std::move(body));
      return;
    }
    if (command == "disconnect" || command == "terminate") {
      done = true;
      paused = false;
      if (interp)
        interp->debug.abort = true;
      writeResponseNamed(id, command, Json::object());
      return;
    }
    writeResponseNamed(id, command, Json::object());
  }

  std::map<std::string, std::vector<Interpreter::DebugBreakpoint>> pendingBreaks;

  void startProgram() {
    if (running || program.empty())
      return;
    running = true;
    try {
      if (!cwd.empty())
        std::filesystem::current_path(cwd);
      std::vector<Diagnostic> parseErrs;
      Program prog = parseSource(readFile(program), program, &parseErrs);
      if (!parseErrs.empty())
        throw std::runtime_error(parseErrs[0].message);
      Interpreter local(std::move(prog), program, args);
      interp = &local;
      interp->debug.enabled = true;
      interp->debug.stopOnEntry = stopOnEntry;
      interp->debug.breakpoints = pendingBreaks;
      interp->debug.pauseAndWait = [this](const std::string &reason) {
        pauseAndWait(reason);
      };
      if (interp->fns.count("main"))
        interp->callNamed("main");
      Json ex = Json::object();
      ex.set("exitCode", Json::num(0));
      writeEvent("exited", std::move(ex));
      writeEvent("terminated", Json::object());
    } catch (const std::exception &ex) {
      Json out = Json::object();
      out.set("category", Json::str("stderr"));
      out.set("output", Json::str(std::string(ex.what()) + "\n"));
      writeEvent("output", std::move(out));
      Json exb = Json::object();
      exb.set("exitCode", Json::num(1));
      writeEvent("exited", std::move(exb));
      writeEvent("terminated", Json::object());
    }
    interp = nullptr;
    running = false;
    done = true;
    paused = false;
  }
};

} // namespace

int runDebugAdapter() {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  setvbuf(stdin, nullptr, _IONBF, 0);
  setvbuf(stdout, nullptr, _IONBF, 0);

  DapServer dap;
  while (!dap.done) {
    dap.processOne(true);
    if (dap.configured && dap.launched && !dap.running)
      dap.startProgram();
  }
  return 0;
}
