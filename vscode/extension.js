const vscode = require("vscode");
const fs = require("fs");
const path = require("path");

function findCli(startDir) {
  const names =
    process.platform === "win32"
      ? ["RoseGoldC.exe", "rosegoldc.exe"]
      : ["RoseGoldC", "rosegoldc"];
  const found = [];
  let dir = startDir;
  for (let i = 0; i < 12 && dir; i++) {
    for (const name of names) {
      const cand = path.join(dir, "build", name);
      if (fs.existsSync(cand)) found.push(cand);
    }
    const parent = path.dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  return found;
}

function newest(paths) {
  if (!paths.length) return null;
  return paths
    .slice()
    .sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs)[0];
}

function resolveCli(hintPath) {
  const cfg = vscode.workspace.getConfiguration("rosegoldc");
  const raw = (cfg.get("cliPath", "") || "").trim();
  if (raw) return raw;

  const dirs = [];
  for (const folder of vscode.workspace.workspaceFolders || []) {
    dirs.push(folder.uri.fsPath);
  }
  if (hintPath) dirs.push(path.dirname(hintPath));

  const found = [];
  for (const dir of dirs) found.push(...findCli(dir));
  const hit = newest(found);
  if (hit) return hit;

  return process.platform === "win32" ? "RoseGoldC.exe" : "RoseGoldC";
}

function workspaceCwd(filePath) {
  const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(filePath));
  if (folder) return folder.uri.fsPath;
  if (vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders.length) {
    return vscode.workspace.workspaceFolders[0].uri.fsPath;
  }
  return path.dirname(filePath);
}

async function activeRgFile() {
  const ed = vscode.window.activeTextEditor;
  if (ed && ed.document.languageId === "rosegold") {
    if (ed.document.isDirty) await ed.document.save();
    return ed.document.uri.fsPath;
  }
  vscode.window.showErrorMessage("Open a .rg file first.");
  return null;
}

async function runCli(subcommand) {
  const file = await activeRgFile();
  if (!file) return;

  const cli = resolveCli(file);
  const abs = path.isAbsolute(cli) ? cli : cli;
  if (path.isAbsolute(abs) && !fs.existsSync(abs)) {
    const pick = await vscode.window.showErrorMessage(
      `RoseGoldC.exe not found at ${abs}. Build with Ctrl+Shift+B, or set RoseGoldC › Cli Path.`,
      "Build"
    );
    if (pick === "Build") {
      await vscode.commands.executeCommand("workbench.action.tasks.build");
    }
    return;
  }

  const cwd = workspaceCwd(file);
  const def = { type: "rosegoldc", task: subcommand };
  const exec = new vscode.ShellExecution(cli, [subcommand, file], { cwd });
  const task = new vscode.Task(
    def,
    vscode.TaskScope.Workspace,
    `RoseGoldC ${subcommand}`,
    "rosegoldc",
    exec
  );
  task.presentationOptions = {
    reveal: vscode.TaskRevealKind.Always,
    panel: vscode.TaskPanelKind.Dedicated,
    clear: true,
  };
  await vscode.tasks.executeTask(task);
}

function commentText(line) {
  const t = line.trim();
  if (t.startsWith("///")) return t.slice(3).trim();
  if (t.startsWith("//")) return t.slice(2).trim();
  if (t.startsWith("##")) return t.slice(2).trim();
  if (t.startsWith("#") && !t.startsWith("#/")) return t.slice(1).trim();
  return null;
}

function docsAbove(lines, line) {
  const docs = [];
  for (let i = line - 1; i >= 0; i--) {
    const t = lines[i].trim();
    if (t.startsWith("@")) continue;
    const text = commentText(lines[i]);
    if (text !== null) docs.unshift(text);
    else if (t === "") {
      if (docs.length) break;
    } else break;
  }
  return docs.filter(Boolean).join("\n");
}

function codeLine(line, state) {
  let out = "";
  for (let i = 0; i < line.length; i++) {
    const a = line[i];
    const b = line[i + 1];
    if (state.inBlock) {
      if (a === "#" && b === "/") {
        state.inBlock = false;
        i++;
      }
      continue;
    }
    if (a === "/" && b === "#") {
      state.inBlock = true;
      i++;
      continue;
    }
    if (a === "/" && b === "/") break;
    if (a === "#") break;
    out += a;
  }
  return out;
}

function symbolsInDocument(doc) {
  const lines = doc.getText().split(/\r?\n/);
  const symbols = [];
  const state = { inBlock: false };
  const declRe = /\b(fn|signal|struct|class|trait)\s+([A-Za-z_][A-Za-z0-9_]*)/;
  const bindRe = /\b(var|const)\s+([A-Za-z_][A-Za-z0-9_]*)/;
  for (let line = 0; line < lines.length; line++) {
    const code = codeLine(lines[line], state);
    const decl = declRe.exec(code);
    if (decl) {
      const name = decl[2];
      const col = lines[line].indexOf(name);
      const params = [];
      if (decl[1] === "fn" || decl[1] === "signal") {
        const paren = code.indexOf("(");
        const close = code.indexOf(")");
        if (paren >= 0 && close > paren) {
          for (const part of code.slice(paren + 1, close).split(",")) {
            const pm = /^\s*([A-Za-z_][A-Za-z0-9_]*)/.exec(part);
            if (pm) params.push(pm[1]);
          }
        }
      }
      symbols.push({
        name,
        kind: decl[1],
        line,
        col: col < 0 ? 0 : col,
        doc: docsAbove(lines, line),
        detail: code.trim(),
        params,
      });
    }
    const bind = bindRe.exec(code);
    if (bind) {
      const name = bind[2];
      const col = lines[line].indexOf(name);
      symbols.push({
        name,
        kind: bind[1],
        line,
        col: col < 0 ? 0 : col,
        doc: docsAbove(lines, line),
        detail: code.trim(),
        params: [],
      });
    }
  }
  return symbols;
}

async function allSymbols(doc) {
  const out = symbolsInDocument(doc);
  const seen = new Set(out.map((s) => `${doc.uri.fsPath}:${s.name}:${s.kind}`));
  const files = await vscode.workspace.findFiles("**/*.rg", "**/build/**");
  for (const uri of files) {
    if (uri.fsPath === doc.uri.fsPath) continue;
    const other = await vscode.workspace.openTextDocument(uri);
    for (const s of symbolsInDocument(other)) {
      const key = `${uri.fsPath}:${s.name}:${s.kind}`;
      if (seen.has(key)) continue;
      seen.add(key);
      out.push({ ...s, uri });
    }
  }
  return out.map((s) => ({ uri: s.uri || doc.uri, ...s }));
}

const definitionProvider = {
  async provideDefinition(doc, position) {
    const word = doc.getWordRangeAtPosition(position);
    if (!word) return;
    const name = doc.getText(word);
    const symbols = await allSymbols(doc);
    const hits = symbols.filter((s) => s.name === name);
    if (!hits.length) return;
    return hits.map(
      (s) =>
        new vscode.Location(
          s.uri,
          new vscode.Position(s.line, s.col)
        )
    );
  },
};

const hoverProvider = {
  async provideHover(doc, position) {
    const word = doc.getWordRangeAtPosition(position);
    if (!word) return;
    const name = doc.getText(word);
    const symbols = await allSymbols(doc);
    const hit = symbols.find((s) => s.name === name);
    if (!hit) return;
    const md = new vscode.MarkdownString();
    md.appendCodeblock(hit.detail || `${hit.kind} ${hit.name}`, "rosegold");
    if (hit.doc) md.appendMarkdown("\n\n" + hit.doc);
    return new vscode.Hover(md, word);
  },
};

const KEYWORDS = [
  "fn",
  "var",
  "const",
  "struct",
  "class",
  "trait",
  "extends",
  "impl",
  "for",
  "super",
  "signal",
  "import",
  "return",
  "pass",
  "break",
  "continue",
  "if",
  "elif",
  "else",
  "while",
  "self",
  "true",
  "false",
];
const TYPES = ["Int", "String", "Bool", "Void"];
const BUILTINS = [
  { label: "print", insert: "print($0)", detail: "print(...)" },
  { label: "assert", insert: "assert($0)", detail: "assert(cond)" },
  { label: "checks.eq", insert: "checks.eq($1, $2)", detail: "checks.eq(a, b)" },
  { label: "checks.neq", insert: "checks.neq($1, $2)", detail: "checks.neq(a, b)" },
  { label: "checks.eq_string", insert: "checks.eq_string($1, $2)", detail: "checks.eq_string(a, b)" },
  { label: "checks.that", insert: "checks.that($0)", detail: "checks.that(cond)" },
  { label: "argv", insert: "argv($0)", detail: "argv(i) — script path at 0" },
  { label: "argv_len", insert: "argv_len()", detail: "argv_len() — argc" },
];

function item(label, kind, detail, doc, insert) {
  const it = new vscode.CompletionItem(label, kind);
  it.detail = detail;
  if (doc) it.documentation = doc;
  if (insert) {
    it.insertText = new vscode.SnippetString(insert);
  }
  return it;
}

function linePrefix(doc, position) {
  return doc.lineAt(position.line).text.slice(0, position.character);
}

const KIND_OF = {
  fn: vscode.CompletionItemKind.Function,
  signal: vscode.CompletionItemKind.Event,
  struct: vscode.CompletionItemKind.Struct,
  class: vscode.CompletionItemKind.Class,
  trait: vscode.CompletionItemKind.Interface,
  var: vscode.CompletionItemKind.Variable,
  const: vscode.CompletionItemKind.Constant,
};

const completionProvider = {
  async provideCompletionItems(doc, position) {
    const prefix = linePrefix(doc, position);
    const symbols = await allSymbols(doc);

    if (/@\w*$/.test(prefix)) {
      return [
        item("test", vscode.CompletionItemKind.Keyword, "@test", "Mark a test function"),
        item(
          "deprecated",
          vscode.CompletionItemKind.Keyword,
          "@deprecated",
          "Warn when this function is called"
        ),
      ];
    }

    const checksDot = /(?:^|[\s,(])checks\.\w*$/.test(prefix);
    if (checksDot) {
      return [
        item("eq", vscode.CompletionItemKind.Method, "checks.eq(a, b)", "", "eq($1, $2)"),
        item("neq", vscode.CompletionItemKind.Method, "checks.neq(a, b)", "", "neq($1, $2)"),
        item(
          "eq_string",
          vscode.CompletionItemKind.Method,
          "checks.eq_string(a, b)",
          "",
          "eq_string($1, $2)"
        ),
        item("that", vscode.CompletionItemKind.Method, "checks.that(cond)", "", "that($0)"),
        item("truthy", vscode.CompletionItemKind.Method, "checks.truthy(cond)", "", "truthy($0)"),
      ];
    }

    const member = /(?:^|[^\w])([A-Za-z_][A-Za-z0-9_]*)\.\w*$/.exec(prefix);
    if (member) {
      const recv = member[1];
      if (recv === "checks") {
        return [];
      }
      const sig = symbols.find((s) => s.name === recv && s.kind === "signal");
      if (sig) {
        const args = (sig.params || []).map((p, i) => `\${${i + 1}:${p}}`).join(", ");
        return [
          item(
            "connect",
            vscode.CompletionItemKind.Method,
            `${recv}.connect(fn)`,
            sig.doc,
            "connect($0)"
          ),
          item(
            "emit",
            vscode.CompletionItemKind.Method,
            `${recv}.emit(${(sig.params || []).join(", ")})`,
            sig.doc,
            `emit(${args})`
          ),
          item(
            "disconnect",
            vscode.CompletionItemKind.Method,
            `${recv}.disconnect(fn)`,
            sig.doc,
            "disconnect($0)"
          ),
        ];
      }
      if (recv === "process") {
        return [
          item(
            "argv",
            vscode.CompletionItemKind.Method,
            "process.argv(i)",
            "Script path at 0, then run args",
            "argv($0)"
          ),
          item(
            "argc",
            vscode.CompletionItemKind.Method,
            "process.argc()",
            "Number of argv entries",
            "argc()"
          ),
        ];
      }
      return [];
    }

    if (/:\s*[A-Za-z_]*$/.test(prefix)) {
      return TYPES.map((t) =>
        item(t, vscode.CompletionItemKind.TypeParameter, "type", "")
      );
    }

    const items = [];
    for (const kw of KEYWORDS) {
      items.push(item(kw, vscode.CompletionItemKind.Keyword, "keyword", ""));
    }
    for (const t of TYPES) {
      items.push(item(t, vscode.CompletionItemKind.TypeParameter, "type", ""));
    }
    for (const b of BUILTINS) {
      items.push(
        item(b.label, vscode.CompletionItemKind.Function, b.detail, "", b.insert)
      );
    }
    const seen = new Set();
    for (const s of symbols) {
      const key = `${s.kind}:${s.name}`;
      if (seen.has(key)) continue;
      seen.add(key);
      const kind = KIND_OF[s.kind] || vscode.CompletionItemKind.Variable;
      let insert = s.name;
      if (s.kind === "fn") {
        const args = (s.params || []).map((p, i) => `\${${i + 1}:${p}}`).join(", ");
        insert = `${s.name}(${args})`;
      } else if (s.kind === "signal") {
        insert = s.name;
      }
      const it = item(s.name, kind, s.detail, s.doc, insert === s.name ? undefined : insert);
      items.push(it);
    }
    return items;
  },
};

function activate(context) {
  context.subscriptions.push(
    vscode.commands.registerCommand("rosegoldc.runFile", () => runCli("run")),
    vscode.commands.registerCommand("rosegoldc.testFile", () => runCli("test")),
    vscode.languages.registerDefinitionProvider("rosegold", definitionProvider),
    vscode.languages.registerHoverProvider("rosegold", hoverProvider),
    vscode.languages.registerCompletionItemProvider(
      "rosegold",
      completionProvider,
      ".",
      "@",
      ":"
    )
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
