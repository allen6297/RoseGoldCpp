#include "format.h"
#include "parser.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace {

struct Printer {
  FormatOptions opts;
  std::ostringstream out;
  int indent = 0;

  void line() { out << "\n"; }
  void pad() {
    for (int i = 0; i < indent; ++i)
      out << "    ";
  }
  void write(const std::string &s) { out << s; }
  void writeln(const std::string &s) {
    pad();
    out << s << "\n";
  }

  static std::string escapeString(const std::string &s) {
    std::string o = "\"";
    for (unsigned char c : s) {
      if (c == '\\')
        o += "\\\\";
      else if (c == '"')
        o += "\\\"";
      else if (c == '\n')
        o += "\\n";
      else if (c == '\r')
        o += "\\r";
      else if (c == '\t')
        o += "\\t";
      else if (c < 32) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\x%02x", c);
        o += buf;
      } else
        o.push_back(static_cast<char>(c));
    }
    o += "\"";
    return o;
  }

  static int binPrec(const std::string &op) {
    if (op == "||")
      return 1;
    if (op == "&&")
      return 2;
    if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" ||
        op == ">=")
      return 3;
    if (op == "+" || op == "-")
      return 4;
    if (op == "*" || op == "/" || op == "%")
      return 5;
    return 0;
  }

  void typeArgs(const std::vector<std::string> &args) {
    if (args.empty())
      return;
    write("[");
    for (size_t i = 0; i < args.size(); ++i) {
      if (i)
        write(", ");
      write(args[i]);
    }
    write("]");
  }

  void typeParams(const std::vector<TypeParam> &params) {
    if (params.empty())
      return;
    write("[");
    for (size_t i = 0; i < params.size(); ++i) {
      if (i)
        write(", ");
      write(params[i].name);
      if (!params[i].bounds.empty()) {
        write(": ");
        for (size_t b = 0; b < params[i].bounds.size(); ++b) {
          if (b)
            write(" + ");
          write(params[i].bounds[b]);
        }
      }
    }
    write("]");
  }

  void expr(const Expr &e, int parentPrec = 0) {
    switch (e.kind) {
    case Expr::Kind::Int:
      write(std::to_string(e.number));
      break;
    case Expr::Kind::Float: {
      if (!e.text.empty()) {
        write(e.text);
      } else {
        std::ostringstream ss;
        ss << e.real;
        write(ss.str());
      }
      break;
    }
    case Expr::Kind::String:
      write(escapeString(e.text));
      break;
    case Expr::Kind::Bool:
      write(e.boolean ? "true" : "false");
      break;
    case Expr::Kind::Var:
      if (!e.module.empty()) {
        write(e.module);
        write(".");
      }
      write(e.text);
      typeArgs(e.typeArgs);
      break;
    case Expr::Kind::Unary:
      write(e.text);
      if (!e.kids.empty())
        expr(e.kids[0], 6);
      break;
    case Expr::Kind::Binary: {
      int prec = binPrec(e.text);
      bool wrap = prec < parentPrec;
      if (wrap)
        write("(");
      if (!e.kids.empty())
        expr(e.kids[0], prec);
      write(" ");
      write(e.text);
      write(" ");
      if (e.kids.size() >= 2)
        expr(e.kids[1], prec + 1);
      if (wrap)
        write(")");
      break;
    }
    case Expr::Kind::Call: {
      size_t start = 0;
      if (!e.text.empty())
        write(e.text);
      else if (!e.kids.empty()) {
        expr(e.kids[0], 7);
        start = 1;
      }
      typeArgs(e.typeArgs);
      write("(");
      for (size_t i = start; i < e.kids.size(); ++i) {
        if (i > start)
          write(", ");
        expr(e.kids[i]);
      }
      write(")");
      break;
    }
    case Expr::Kind::MethodCall:
      if (!e.kids.empty())
        expr(e.kids[0], 7);
      write(".");
      write(e.text);
      typeArgs(e.typeArgs);
      write("(");
      for (size_t i = 1; i < e.kids.size(); ++i) {
        if (i > 1)
          write(", ");
        expr(e.kids[i]);
      }
      write(")");
      break;
    case Expr::Kind::Member:
      if (!e.kids.empty())
        expr(e.kids[0], 7);
      write(".");
      write(e.text);
      break;
    case Expr::Kind::Index:
      if (!e.kids.empty())
        expr(e.kids[0], 7);
      write("[");
      if (e.kids.size() >= 2)
        expr(e.kids[1]);
      write("]");
      break;
    case Expr::Kind::Array:
      write("[");
      for (size_t i = 0; i < e.kids.size(); ++i) {
        if (i)
          write(", ");
        expr(e.kids[i]);
      }
      write("]");
      break;
    case Expr::Kind::Map:
      write("{");
      for (size_t i = 0; i + 1 < e.kids.size(); i += 2) {
        if (i)
          write(", ");
        expr(e.kids[i]);
        write(": ");
        expr(e.kids[i + 1]);
      }
      write("}");
      break;
    case Expr::Kind::StructLit:
      write(e.text);
      typeArgs(e.typeArgs);
      write(" {");
      if (!e.names.empty()) {
        write(" ");
        for (size_t i = 0; i < e.names.size(); ++i) {
          if (i)
            write(", ");
          write(e.names[i]);
          write(": ");
          if (i < e.kids.size())
            expr(e.kids[i]);
        }
        write(" ");
      }
      write("}");
      break;
    case Expr::Kind::Range:
      if (!e.kids.empty())
        expr(e.kids[0]);
      write("..");
      if (e.kids.size() >= 2)
        expr(e.kids[1]);
      break;
    case Expr::Kind::Try:
      write("try ");
      if (!e.kids.empty())
        expr(e.kids[0], 7);
      break;
    case Expr::Kind::Await:
      write("await ");
      if (!e.kids.empty())
        expr(e.kids[0], 7);
      break;
    case Expr::Kind::Lambda:
      if (e.lambda)
        fnInline(*e.lambda);
      else
        write("fn () {}");
      break;
    }
  }

  void blockOpen() {
    write("{\n");
    ++indent;
  }
  void blockClose(bool newlineAfter) {
    --indent;
    pad();
    write("}");
    if (newlineAfter)
      line();
  }

  void stmts(const std::vector<Stmt> &body) {
    for (const auto &s : body)
      stmt(s);
  }

  void endStmt(const Stmt &s) {
    write(";");
    if (opts.keepComments && !s.trailingComment.empty()) {
      write(" ");
      write(s.trailingComment);
    }
    write("\n");
  }

  void stmt(const Stmt &s) {
    if (opts.keepComments)
      emitComments(s.leadingComments);
    switch (s.kind) {
    case Stmt::Kind::Comment:
      break;
    case Stmt::Kind::Expr:
      pad();
      expr(s.expr);
      endStmt(s);
      break;
    case Stmt::Kind::Var:
    case Stmt::Kind::Const:
      pad();
      write(s.kind == Stmt::Kind::Const ? "const " : "var ");
      write(s.name);
      if (!s.typeName.empty()) {
        write(": ");
        write(s.typeName);
      }
      write(" = ");
      expr(s.expr);
      endStmt(s);
      break;
    case Stmt::Kind::Assign:
      pad();
      write(s.name);
      write(" ");
      write(s.op);
      write(" ");
      expr(s.expr);
      endStmt(s);
      break;
    case Stmt::Kind::FieldAssign:
      pad();
      expr(s.target);
      write(".");
      write(s.name);
      write(" ");
      write(s.op);
      write(" ");
      expr(s.expr);
      endStmt(s);
      break;
    case Stmt::Kind::IndexAssign:
      pad();
      if (s.target.kind == Expr::Kind::Index && s.target.kids.size() >= 2) {
        expr(s.target.kids[0], 7);
        write("[");
        expr(s.target.kids[1]);
        write("]");
      } else
        expr(s.target);
      write(" ");
      write(s.op);
      write(" ");
      expr(s.expr);
      endStmt(s);
      break;
    case Stmt::Kind::Return:
      pad();
      write("return");
      if (s.hasExpr) {
        write(" ");
        expr(s.expr);
      }
      endStmt(s);
      break;
    case Stmt::Kind::If:
      pad();
      write("if (");
      expr(s.expr);
      write(") ");
      blockOpen();
      stmts(s.body);
      blockClose(false);
      if (!s.elseBody.empty()) {
        if (s.elseBody.size() == 1 && s.elseBody[0].kind == Stmt::Kind::If) {
          const Stmt *cur = &s.elseBody[0];
          while (true) {
            write(" elif (");
            expr(cur->expr);
            write(") ");
            blockOpen();
            stmts(cur->body);
            blockClose(false);
            if (cur->elseBody.size() == 1 &&
                cur->elseBody[0].kind == Stmt::Kind::If) {
              cur = &cur->elseBody[0];
              continue;
            }
            if (!cur->elseBody.empty()) {
              write(" else ");
              blockOpen();
              stmts(cur->elseBody);
              blockClose(false);
            }
            break;
          }
        } else {
          write(" else ");
          blockOpen();
          stmts(s.elseBody);
          blockClose(false);
        }
      }
      line();
      break;
    case Stmt::Kind::While:
      pad();
      write("while (");
      expr(s.expr);
      write(") ");
      blockOpen();
      stmts(s.body);
      blockClose(true);
      break;
    case Stmt::Kind::For:
      pad();
      write("for (");
      write(s.name);
      write(" in ");
      expr(s.expr);
      write(") ");
      blockOpen();
      stmts(s.body);
      blockClose(true);
      break;
    case Stmt::Kind::Match:
      pad();
      write("match ");
      expr(s.expr);
      write(" {\n");
      ++indent;
      for (const auto &arm : s.arms) {
        pad();
        switch (arm.pat) {
        case MatchArm::Pat::Wildcard:
          write("_");
          break;
        case MatchArm::Pat::Int:
          write(std::to_string(arm.number));
          break;
        case MatchArm::Pat::Float: {
          if (!arm.text.empty()) {
            write(arm.text);
          } else {
            std::ostringstream ss;
            ss << arm.real;
            write(ss.str());
          }
          break;
        }
        case MatchArm::Pat::String:
          write(escapeString(arm.text));
          break;
        case MatchArm::Pat::Bool:
          write(arm.boolean ? "true" : "false");
          break;
        case MatchArm::Pat::Variant:
          write(arm.name);
          if (!arm.fieldNames.empty()) {
            write("(");
            for (size_t i = 0; i < arm.fieldNames.size(); ++i) {
              if (i)
                write(", ");
              write(arm.fieldNames[i]);
              if (i < arm.binds.size() && !arm.binds[i].empty()) {
                write(": ");
                write(arm.binds[i]);
              }
            }
            write(")");
          } else if (!arm.binds.empty()) {
            write("(");
            for (size_t i = 0; i < arm.binds.size(); ++i) {
              if (i)
                write(", ");
              write(arm.binds[i]);
            }
            write(")");
          }
          break;
        }
        write(" {\n");
        ++indent;
        stmts(arm.body);
        --indent;
        writeln("}");
      }
      --indent;
      writeln("}");
      break;
    case Stmt::Kind::Pass:
      pad();
      write("pass");
      endStmt(s);
      break;
    case Stmt::Kind::Break:
      pad();
      write("break");
      endStmt(s);
      break;
    case Stmt::Kind::Continue:
      pad();
      write("continue");
      endStmt(s);
      break;
    case Stmt::Kind::Throw:
      pad();
      write("throw ");
      expr(s.expr);
      endStmt(s);
      break;
    case Stmt::Kind::Do:
      pad();
      write("do ");
      blockOpen();
      stmts(s.body);
      blockClose(false);
      if (!s.name.empty()) {
        write(" catch ");
        write(s.name);
        write(" ");
        blockOpen();
        stmts(s.elseBody);
        blockClose(false);
      }
      line();
      break;
    }
  }

  void fnInline(const FnDecl &fn) {
    if (fn.isAsync)
      write("async ");
    write("fn (");
    for (size_t i = 0; i < fn.params.size(); ++i) {
      if (i)
        write(", ");
      write(fn.params[i]);
      if (i < fn.paramTypes.size() && !fn.paramTypes[i].empty()) {
        write(": ");
        write(fn.paramTypes[i]);
      }
    }
    write(")");
    if (fn.throws)
      write(" throws");
    if (!fn.returnType.empty()) {
      write(": ");
      write(fn.returnType);
    }
    write(" ");
    blockOpen();
    stmts(fn.body);
    blockClose(false);
  }

  void visPrefix(Vis v, bool isPub) {
    if (v == Vis::Private || !isPub)
      write("private ");
    else if (v == Vis::Protected)
      write("protected ");
  }

  void fn(const FnDecl &fn) {
    if (fn.isTest) {
      writeln("@test");
    }
    if (fn.isDeprecated) {
      writeln("@deprecated");
    }
    if (fn.isConstexpr) {
      writeln("@constexpr");
    }
    if (fn.isUfcs) {
      writeln("@ufcs");
    }
    pad();
    if (fn.isAbstract)
      write("abstract ");
    if (fn.isFinal)
      write("final ");
    visPrefix(fn.vis, fn.isPub);
    if (fn.isAsync)
      write("async ");
    write("fn ");
    write(fn.name);
    typeParams(fn.typeParams);
    write("(");
    for (size_t i = 0; i < fn.params.size(); ++i) {
      if (i)
        write(", ");
      write(fn.params[i]);
      if (i < fn.paramTypes.size() && !fn.paramTypes[i].empty()) {
        write(": ");
        write(fn.paramTypes[i]);
      }
    }
    write(")");
    if (fn.throws)
      write(" throws");
    if (!fn.returnType.empty()) {
      write(": ");
      write(fn.returnType);
    }
    if (fn.isAbstract) {
      write(";\n");
      return;
    }
    write(" ");
    blockOpen();
    stmts(fn.body);
    blockClose(true);
  }

  void signal(const SignalDecl &sig) {
    pad();
    if (!sig.isPub)
      write("private ");
    write("signal ");
    write(sig.name);
    write("(");
    for (size_t i = 0; i < sig.params.size(); ++i) {
      if (i)
        write(", ");
      write(sig.params[i]);
    }
    write(");\n");
  }

  void importDecl(const ImportDecl &im) {
    pad();
    if (im.isFrom) {
      write("from ");
      if (!im.path.empty())
        write(im.path[0]);
      write(" import ");
      for (size_t i = 1; i < im.path.size(); ++i) {
        if (i > 1)
          write(".");
        write(im.path[i]);
      }
    } else {
      write("import ");
      for (size_t i = 0; i < im.path.size(); ++i) {
        if (i)
          write(".");
        write(im.path[i]);
      }
    }
    if (!im.alias.empty()) {
      write(" as ");
      write(im.alias);
    }
    write(";\n");
  }

  void structDeclPrint(const structDecl &s) {
    pad();
    if (!s.isPub)
      write("private ");
    write(s.isData ? "data " : "struct ");
    write(s.name);
    typeParams(s.typeParams);
    if (!s.implTraits.empty()) {
      write(" impl ");
      for (size_t i = 0; i < s.implTraits.size(); ++i) {
        if (i)
          write(", ");
        write(s.implTraits[i]);
      }
    }
    write(" {\n");
    ++indent;
    for (size_t i = 0; i < s.fields.size(); ++i) {
      pad();
      if (i < s.fieldVis.size()) {
        Vis v = static_cast<Vis>(s.fieldVis[i]);
        if (v == Vis::Private)
          write("private ");
        else if (v == Vis::Protected)
          write("protected ");
      }
      if (i < s.fieldOptional.size() && s.fieldOptional[i])
        write("@optional ");
      write(s.fields[i]);
      if (i < s.fieldTypes.size() && !s.fieldTypes[i].empty()) {
        write(": ");
        write(s.fieldTypes[i]);
      }
      write(";\n");
    }
    for (const auto &sig : s.signals)
      signal(sig);
    for (const auto &m : s.methods) {
      line();
      fn(m);
    }
    --indent;
    writeln("}");
  }

  void classDeclPrint(const ClassDecl &c) {
    pad();
    if (c.isAbstract)
      write("abstract ");
    if (c.isFinal)
      write("final ");
    if (!c.isPub)
      write("private ");
    write("class ");
    write(c.name);
    typeParams(c.typeParams);
    if (!c.parent.empty()) {
      write(" extends ");
      write(c.parent);
    }
    if (!c.implTraits.empty()) {
      write(" impl ");
      for (size_t i = 0; i < c.implTraits.size(); ++i) {
        if (i)
          write(", ");
        write(c.implTraits[i]);
      }
    }
    write(" {\n");
    ++indent;
    for (const auto &f : c.fields) {
      pad();
      if (f.vis == Vis::Private)
        write("private ");
      else if (f.vis == Vis::Protected)
        write("protected ");
      if (f.optional)
        write("@optional ");
      write("var ");
      write(f.name);
      if (!f.type.empty()) {
        write(": ");
        write(f.type);
      }
      if (f.hasDefault) {
        write(" = ");
        expr(f.defaultValue);
      }
      write(";\n");
    }
    for (const auto &sig : c.signals)
      signal(sig);
    for (const auto &m : c.methods) {
      line();
      fn(m);
    }
    for (const auto &ti : c.traitImpls) {
      line();
      pad();
      write("impl ");
      write(ti.traitName);
      typeParams(ti.typeParams);
      write(" {\n");
      ++indent;
      for (const auto &m : ti.methods)
        fn(m);
      --indent;
      writeln("}");
    }
    --indent;
    writeln("}");
  }

  void traitDeclPrint(const TraitDecl &t) {
    pad();
    if (!t.isPub)
      write("private ");
    write("trait ");
    write(t.name);
    typeParams(t.typeParams);
    write(" {\n");
    ++indent;
    for (const auto &m : t.methods) {
      pad();
      write("fn ");
      write(m.name);
      write("(");
      for (size_t i = 0; i < m.params.size(); ++i) {
        if (i)
          write(", ");
        write(m.params[i]);
        if (i < m.paramTypes.size() && !m.paramTypes[i].empty()) {
          write(": ");
          write(m.paramTypes[i]);
        }
      }
      write(")");
      if (m.throws)
        write(" throws");
      if (!m.returnType.empty()) {
        write(": ");
        write(m.returnType);
      }
      write(";\n");
    }
    for (const auto &sig : t.signals)
      signal(sig);
    --indent;
    writeln("}");
  }

  void enumDeclPrint(const EnumDecl &e) {
    pad();
    if (!e.isPub)
      write("private ");
    write("enum ");
    write(e.name);
    write(" {\n");
    ++indent;
    for (size_t i = 0; i < e.variants.size(); ++i) {
      pad();
      write(e.variants[i].name);
      if (!e.variants[i].fieldNames.empty()) {
        write("(");
        for (size_t f = 0; f < e.variants[i].fieldNames.size(); ++f) {
          if (f)
            write(", ");
          write(e.variants[i].fieldNames[f]);
        }
        write(")");
      } else if (e.variants[i].arity > 0) {
        write("(");
        for (int a = 0; a < e.variants[i].arity; ++a) {
          if (a)
            write(", ");
          write("_");
        }
        write(")");
      }
      if (i + 1 < e.variants.size())
        write(",");
      line();
    }
    --indent;
    writeln("}");
  }

  void implDeclPrint(const ImplDecl &im) {
    pad();
    write("impl");
    typeParams(im.typeParams);
    write(" ");
    if (!im.traitName.empty()) {
      write(im.traitName);
      write(" for ");
      write(im.typeName);
    } else
      write(im.typeName);
    write(" {\n");
    ++indent;
    for (const auto &m : im.methods)
      fn(m);
    --indent;
    writeln("}");
  }

  void modDeclPrint(const ModDecl &m);

  void emitComments(const std::vector<std::string> &comments) {
    if (!opts.keepComments)
      return;
    for (const auto &c : comments) {
      pad();
      write(c);
      write("\n");
    }
  }

  void emitItems(const std::vector<OrderedItem> &order,
                 const std::vector<ImportDecl> &imports,
                 const std::vector<FnDecl> &fns,
                 const std::vector<structDecl> &structs,
                 const std::vector<ClassDecl> &classes,
                 const std::vector<TraitDecl> &traits,
                 const std::vector<EnumDecl> &enums,
                 const std::vector<ImplDecl> &impls,
                 const std::vector<SignalDecl> &signals,
                 const std::vector<ModDecl> &mods,
                 const std::vector<std::string> &trailingComments = {}) {
    ItemKind prev = ItemKind::Import;
    bool first = true;
    for (const auto &it : order) {
      bool blank =
          opts.blankBetweenItems && !first &&
          !(prev == ItemKind::Import && it.kind == ItemKind::Import);
      if (blank)
        line();
      first = false;
      prev = it.kind;
      emitComments(it.leadingComments);
      switch (it.kind) {
      case ItemKind::Import:
        if (it.index < imports.size())
          importDecl(imports[it.index]);
        break;
      case ItemKind::Fn:
        if (it.index < fns.size())
          fn(fns[it.index]);
        break;
      case ItemKind::Struct:
        if (it.index < structs.size())
          structDeclPrint(structs[it.index]);
        break;
      case ItemKind::Class:
        if (it.index < classes.size())
          classDeclPrint(classes[it.index]);
        break;
      case ItemKind::Trait:
        if (it.index < traits.size())
          traitDeclPrint(traits[it.index]);
        break;
      case ItemKind::Enum:
        if (it.index < enums.size())
          enumDeclPrint(enums[it.index]);
        break;
      case ItemKind::Impl:
        if (it.index < impls.size())
          implDeclPrint(impls[it.index]);
        break;
      case ItemKind::Signal:
        if (it.index < signals.size())
          signal(signals[it.index]);
        break;
      case ItemKind::Mod:
        if (it.index < mods.size())
          modDeclPrint(mods[it.index]);
        break;
      }
    }
    if (opts.keepComments && !trailingComments.empty()) {
      if (!first)
        line();
      emitComments(trailingComments);
    }
  }

  void program(const Program &p) {
    emitItems(p.items, p.imports, p.fns, p.structs, p.classes, p.traits, p.enums,
              p.impls, p.signals, p.mods, p.trailingComments);
  }
};

void Printer::modDeclPrint(const ModDecl &m) {
  pad();
  if (!m.isPub)
    write("private ");
  write("mod ");
  write(m.name);
  write(" {\n");
  ++indent;
  emitItems(m.items, m.imports, m.fns, m.structs, m.classes, m.traits, m.enums,
            m.impls, m.signals, m.mods, m.trailingComments);
  --indent;
  writeln("}");
}

} // namespace

std::string formatProgram(const Program &program, const FormatOptions &opts) {
  Printer p;
  p.opts = opts;
  p.program(program);
  std::string s = p.out.str();
  if (!s.empty() && s.back() != '\n')
    s.push_back('\n');
  return s;
}

FormatResult formatSource(const std::string &source, const std::string &path,
                          const FormatOptions &opts) {
  FormatResult r;
  std::vector<Diagnostic> diags;
  Program program = parseSource(source, path, &diags);
  if (!diags.empty()) {
    r.ok = false;
    r.exitCode = 1;
    r.message = diags[0].message;
    return r;
  }
  r.out = formatProgram(program, opts);
  return r;
}

FormatResult formatFile(const std::string &path, const FormatOptions &opts) {
  std::ifstream in(path);
  if (!in) {
    FormatResult r;
    r.ok = false;
    r.exitCode = 2;
    r.message = "cannot open " + path;
    return r;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return formatSource(ss.str(), path, opts);
}
