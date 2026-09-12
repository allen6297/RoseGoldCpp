const vscode = require("vscode");
const { spawn } = require("child_process");
const fs = require("fs");
const path = require("path");

/** @type {vscode.DiagnosticCollection} */
let diagnostics;
/** @type {NodeJS.Timeout | undefined} */
let diagTimer;
/** @type {vscode.StatusBarItem} */
let statusBar;
/** @type {vscode.OutputChannel | undefined} */
let lspLog;
/** @type {{ child: import("child_process").ChildProcess, nextId: number, pending: Map<number, {resolve: Function, reject: Function}>, buf: Buffer, ready: boolean } | null} */
let rpc = null;
let lspStopping = false;

function findCli(startDir) {
  const found = [];
  let dir = startDir;
  for (let i = 0; i < 12 && dir; i++) {
    const buildDir = path.join(dir, "build");
    try {
      for (const name of fs.readdirSync(buildDir)) {
        const lower = name.toLowerCase();
        const isWinExe =
          process.platform === "win32" &&
          lower.startsWith("rosegoldc") &&
          lower.endsWith(".exe");
        const isUnix =
          process.platform !== "win32" &&
          (name === "RoseGoldC" || name === "rosegoldc");
        if (isWinExe || isUnix)
          found.push(path.join(buildDir, name));
      }
    } catch (_) {
      /* missing build dir */
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

async function runCli(subcommand, filePath) {
  let file = filePath;
  if (file && typeof file !== "string")
    file = file.fsPath || (file.toString && file.toString()) || "";
  if (file && /^file:/i.test(file)) {
    try {
      file = vscode.Uri.parse(file).fsPath;
    } catch (_) {
      /* keep */
    }
  }
  if (!file) {
    file = await activeRgFile();
    if (!file) return;
  } else {
    const open = vscode.workspace.textDocuments.find(
      (d) => d.uri.fsPath === file || d.uri.toString() === String(filePath)
    );
    if (open && open.isDirty) await open.save();
  }

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
  task.problemMatchers = ["$rosegoldc", "$rosegoldc-located"];
  await vscode.tasks.executeTask(task);
}

function filePathOf(doc) {
  return doc.uri.scheme === "file" ? doc.uri.fsPath : doc.fileName;
}

function updateStatus(items) {
  if (!statusBar) return;
  const errs = items.filter(
    (i) => i.severity === vscode.DiagnosticSeverity.Error
  ).length;
  const warns = items.length - errs;
  if (errs) {
    statusBar.text = `$(error) RoseGoldC ${errs}`;
    statusBar.tooltip = `${errs} error(s), ${warns} warning(s)`;
  } else if (warns) {
    statusBar.text = `$(warning) RoseGoldC ${warns}`;
    statusBar.tooltip = `${warns} warning(s)`;
  } else {
    statusBar.text = "$(check) RoseGoldC";
    statusBar.tooltip = "No problems";
  }
}

function applyPublishDiagnostics(params) {
  if (!params || !params.uri || !diagnostics) return;
  const uri = vscode.Uri.parse(params.uri);
  const items = (params.diagnostics || []).map((d) => {
    const start =
      d.range && d.range.start ? d.range.start : { line: 0, character: 0 };
    const end = d.range && d.range.end ? d.range.end : start;
    const sev =
      d.severity === 2
        ? vscode.DiagnosticSeverity.Warning
        : vscode.DiagnosticSeverity.Error;
    const diag = new vscode.Diagnostic(
      new vscode.Range(
        Math.max(0, start.line || 0),
        Math.max(0, start.character || 0),
        Math.max(0, end.line || 0),
        Math.max(0, end.character || 0)
      ),
      d.message || "error",
      sev
    );
    diag.source = d.source || "rosegoldc";
    return diag;
  });
  diagnostics.set(uri, items);
  const ed = vscode.window.activeTextEditor;
  if (ed && ed.document.uri.toString() === uri.toString()) {
    updateStatus(items);
  } else if (ed && ed.document.languageId === "rosegold") {
    updateStatus(diagnostics.get(ed.document.uri) || []);
  }
}

function pullMessage(state) {
  const s = state.buf;
  const sep = s.indexOf("\r\n\r\n");
  if (sep < 0) return null;
  const header = s.slice(0, sep).toString("utf8");
  const m = /Content-Length:\s*(\d+)/i.exec(header);
  if (!m) {
    state.buf = s.slice(sep + 4);
    return null;
  }
  const len = Number(m[1]);
  const start = sep + 4;
  if (s.length < start + len) return null;
  const body = s.slice(start, start + len).toString("utf8");
  state.buf = s.slice(start + len);
  return JSON.parse(body);
}

function sendRpc(obj) {
  if (!rpc || !rpc.child.stdin || !rpc.child.stdin.writable) return;
  const body = Buffer.from(JSON.stringify(obj), "utf8");
  const head = Buffer.from(`Content-Length: ${body.length}\r\n\r\n`, "utf8");
  rpc.child.stdin.write(Buffer.concat([head, body]));
}

function notify(method, params) {
  sendRpc({ jsonrpc: "2.0", method, params });
}

function request(method, params) {
  if (!rpc) return Promise.reject(new Error("LSP not running"));
  const id = rpc.nextId++;
  return new Promise((resolve, reject) => {
    rpc.pending.set(id, { resolve, reject });
    sendRpc({ jsonrpc: "2.0", id, method, params });
  });
}

function dispatchRpc(msg) {
  if (msg.id != null && (msg.result !== undefined || msg.error)) {
    const pending = rpc && rpc.pending.get(msg.id);
    if (pending) {
      rpc.pending.delete(msg.id);
      if (msg.error) pending.reject(new Error(msg.error.message || "LSP error"));
      else pending.resolve(msg.result);
    }
    return;
  }
  if (msg.method === "textDocument/publishDiagnostics") {
    applyPublishDiagnostics(msg.params);
  }
}

function didOpen(doc) {
  if (!rpc || !rpc.ready || doc.languageId !== "rosegold") return;
  notify("textDocument/didOpen", {
    textDocument: {
      uri: doc.uri.toString(),
      languageId: "rosegold",
      version: doc.version,
      text: doc.getText(),
    },
  });
}

function didChange(doc) {
  if (!rpc || !rpc.ready || doc.languageId !== "rosegold") return;
  notify("textDocument/didChange", {
    textDocument: { uri: doc.uri.toString(), version: doc.version },
    contentChanges: [{ text: doc.getText() }],
  });
}

function didClose(doc) {
  if (!rpc || !rpc.ready || doc.languageId !== "rosegold") return;
  notify("textDocument/didClose", {
    textDocument: { uri: doc.uri.toString() },
  });
}

function scheduleDiagnostics(doc) {
  if (doc.languageId !== "rosegold") return;
  clearTimeout(diagTimer);
  const delay = vscode.workspace
    .getConfiguration("rosegoldc")
    .get("diagnosticsDelayMs", 400);
  diagTimer = setTimeout(() => didChange(doc), delay);
}

function refreshDiagnostics(doc) {
  if (doc.languageId !== "rosegold") return;
  didChange(doc);
}

function startLanguageServer() {
  const hint =
    (vscode.workspace.workspaceFolders &&
      vscode.workspace.workspaceFolders[0] &&
      vscode.workspace.workspaceFolders[0].uri.fsPath) ||
    "";
  const cli = resolveCli(hint);
  if (!lspLog) lspLog = vscode.window.createOutputChannel("RoseGoldC LSP");
  lspLog.appendLine(`starting ${cli} lsp`);
  lspStopping = false;

  const child = spawn(cli, ["lsp"], {
    cwd: hint || undefined,
    env: process.env,
    stdio: ["pipe", "pipe", "pipe"],
  });
  rpc = {
    child,
    nextId: 1,
    pending: new Map(),
    buf: Buffer.alloc(0),
    ready: false,
  };

  child.stdout.on("data", (chunk) => {
    if (!rpc) return;
    rpc.buf = Buffer.concat([rpc.buf, chunk]);
    while (true) {
      let msg;
      try {
        msg = pullMessage(rpc);
      } catch (err) {
        lspLog.appendLine(String(err));
        break;
      }
      if (!msg) break;
      dispatchRpc(msg);
    }
  });
  child.stderr.on("data", (d) => {
    if (lspLog) lspLog.append(d.toString());
  });
  child.on("error", (err) => {
    if (statusBar) {
      statusBar.text = "$(error) RoseGoldC";
      statusBar.tooltip = `${err.message} (cmd=${cli}). Build with Ctrl+Shift+B, or set RoseGoldC › Cli Path.`;
    }
    lspLog.appendLine(String(err));
  });
  child.on("close", (code) => {
    if (rpc && rpc.child === child) {
      rpc.ready = false;
      rpc = null;
    }
    lspLog.appendLine(`lsp exited ${code}`);
    if (lspStopping) return;
    if (statusBar) {
      statusBar.text = "$(error) RoseGoldC";
      statusBar.tooltip = `language server exited (${code ?? "?"})`;
    }
  });

  const root =
    vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders[0];
  const init = request("initialize", {
    processId: process.pid,
    rootUri: root ? root.uri.toString() : null,
    capabilities: {
      textDocument: { publishDiagnostics: {} },
    },
  });
  const timeout = new Promise((_, reject) =>
    setTimeout(() => reject(new Error("initialize timed out")), 8000)
  );
  return Promise.race([init, timeout])
    .then(() => {
      if (!rpc || rpc.child !== child) return;
      notify("initialized", {});
      rpc.ready = true;
      if (statusBar) {
        statusBar.text = "$(check) RoseGoldC";
        statusBar.tooltip = "Language server ready";
      }
      for (const doc of vscode.workspace.textDocuments) didOpen(doc);
    })
    .catch((err) => {
      if (statusBar) {
        statusBar.text = "$(error) RoseGoldC";
        statusBar.tooltip = String(err.message || err);
      }
      lspLog.appendLine(String(err));
      try {
        child.kill();
      } catch (_) {
        /* ignore */
      }
    });
}

async function stopLanguageServer() {
  if (!rpc) return;
  lspStopping = true;
  const child = rpc.child;
  const shutdown = request("shutdown", null).catch(() => {});
  const timeout = new Promise((resolve) => setTimeout(resolve, 800));
  try {
    await Promise.race([shutdown, timeout]);
    notify("exit", undefined);
  } catch (_) {
    /* ignore */
  }
  rpc.ready = false;
  try {
    child.kill();
  } catch (_) {
    /* ignore */
  }
  rpc = null;
}

function lspRange(r) {
  if (!r || !r.start) return undefined;
  const e = r.end || r.start;
  return new vscode.Range(
    Math.max(0, r.start.line || 0),
    Math.max(0, r.start.character || 0),
    Math.max(0, e.line || 0),
    Math.max(0, e.character || 0)
  );
}

function markupText(contents) {
  if (!contents) return "";
  if (typeof contents === "string") return contents;
  if (Array.isArray(contents)) {
    return contents
      .map((c) => (typeof c === "string" ? c : c.value || c.language || ""))
      .join("\n\n");
  }
  return contents.value || "";
}

const definitionProvider = {
  async provideDefinition(doc, position) {
    if (!rpc || !rpc.ready) return;
    const payload = await request("textDocument/definition", {
      textDocument: { uri: doc.uri.toString() },
      position: { line: position.line, character: position.character },
    });
    if (!payload) return;
    const locs = Array.isArray(payload) ? payload : [payload];
    return locs
      .filter((l) => l && l.uri && l.range)
      .map(
        (l) => new vscode.Location(vscode.Uri.parse(l.uri), lspRange(l.range))
      );
  },
};

const hoverProvider = {
  async provideHover(doc, position) {
    if (!rpc || !rpc.ready) return;
    const payload = await request("textDocument/hover", {
      textDocument: { uri: doc.uri.toString() },
      position: { line: position.line, character: position.character },
    });
    if (!payload || !payload.contents) return;
    const md = new vscode.MarkdownString(markupText(payload.contents));
    md.supportHtml = false;
    return new vscode.Hover(md, lspRange(payload.range));
  },
};

const signatureHelpProvider = {
  async provideSignatureHelp(doc, position) {
    if (!rpc || !rpc.ready) return;
    const payload = await request("textDocument/signatureHelp", {
      textDocument: { uri: doc.uri.toString() },
      position: { line: position.line, character: position.character },
    });
    if (!payload || !payload.signatures) return;
    const help = new vscode.SignatureHelp();
    help.activeSignature = payload.activeSignature || 0;
    help.activeParameter = payload.activeParameter || 0;
    help.signatures = payload.signatures.map((s) => {
      const info = new vscode.SignatureInformation(s.label || "");
      if (s.documentation) {
        info.documentation =
          typeof s.documentation === "string"
            ? s.documentation
            : new vscode.MarkdownString(
                (s.documentation && s.documentation.value) || ""
              );
      }
      info.parameters = (s.parameters || []).map(
        (p) =>
          new vscode.ParameterInformation(
            typeof p.label === "string" ? p.label : p.label[0] || ""
          )
      );
      return info;
    });
    return help;
  },
};

const referenceProvider = {
  async provideReferences(doc, position, context) {
    if (!rpc || !rpc.ready) return;
    const payload = await request("textDocument/references", {
      textDocument: { uri: doc.uri.toString() },
      position: { line: position.line, character: position.character },
      context: {
        includeDeclaration: !context || context.includeDeclaration !== false,
      },
    });
    if (!payload) return;
    const locs = Array.isArray(payload) ? payload : [payload];
    return locs
      .filter((l) => l && l.uri && l.range)
      .map(
        (l) => new vscode.Location(vscode.Uri.parse(l.uri), lspRange(l.range))
      );
  },
};

const completionProvider = {
  async provideCompletionItems(doc, position, token, context) {
    if (!rpc || !rpc.ready) return;
    const payload = await request("textDocument/completion", {
      textDocument: { uri: doc.uri.toString() },
      position: { line: position.line, character: position.character },
      context: {
        triggerKind: (context && context.triggerKind) || 1,
        triggerCharacter: context && context.triggerCharacter,
      },
    });
    if (!payload) return;
    const raw = Array.isArray(payload) ? payload : payload.items || [];
    return raw.map((c) => {
      const it = new vscode.CompletionItem(
        c.label,
        c.kind == null ? vscode.CompletionItemKind.Text : c.kind
      );
      if (c.detail) it.detail = c.detail;
      if (c.documentation) {
        it.documentation =
          typeof c.documentation === "string"
            ? c.documentation
            : new vscode.MarkdownString(c.documentation.value || "");
      }
      if (c.insertText) {
        it.insertText =
          c.insertTextFormat === 2
            ? new vscode.SnippetString(c.insertText)
            : c.insertText;
      }
      return it;
    });
  },
};

function lspWorkspaceEdit(edit) {
  const we = new vscode.WorkspaceEdit();
  if (!edit || !edit.changes) return we;
  for (const [uri, edits] of Object.entries(edit.changes)) {
    const u = vscode.Uri.parse(uri);
    for (const e of edits || []) {
      const r = lspRange(e.range);
      if (r) we.replace(u, r, e.newText || "");
    }
  }
  return we;
}

function lspDocumentSymbol(s) {
  const sel = lspRange(s.selectionRange || s.range);
  const range = lspRange(s.range) || sel;
  const it = new vscode.DocumentSymbol(
    s.name,
    s.detail || "",
    s.kind == null ? vscode.SymbolKind.Function : s.kind,
    range,
    sel || range
  );
  it.children = (s.children || []).map(lspDocumentSymbol);
  return it;
}

const documentSymbolProvider = {
  async provideDocumentSymbols(doc) {
    if (!rpc || !rpc.ready) return;
    const payload = await request("textDocument/documentSymbol", {
      textDocument: { uri: doc.uri.toString() },
    });
    if (!payload) return;
    const raw = Array.isArray(payload) ? payload : [];
    return raw.filter((s) => s && s.name).map(lspDocumentSymbol);
  },
};

const documentHighlightProvider = {
  async provideDocumentHighlights(doc, position) {
    if (!rpc || !rpc.ready) return;
    const payload = await request("textDocument/documentHighlight", {
      textDocument: { uri: doc.uri.toString() },
      position: { line: position.line, character: position.character },
    });
    if (!payload) return;
    const raw = Array.isArray(payload) ? payload : [];
    return raw
      .filter((h) => h && h.range)
      .map(
        (h) =>
          new vscode.DocumentHighlight(
            lspRange(h.range),
            h.kind === 3
              ? vscode.DocumentHighlightKind.Write
              : h.kind === 2
                ? vscode.DocumentHighlightKind.Read
                : vscode.DocumentHighlightKind.Text
          )
      );
  },
};

const renameProvider = {
  async prepareRename(doc, position) {
    if (!rpc || !rpc.ready) throw new Error("Language server not ready");
    const payload = await request("textDocument/prepareRename", {
      textDocument: { uri: doc.uri.toString() },
      position: { line: position.line, character: position.character },
    });
    if (!payload) throw new Error("Cannot rename this symbol");
    const r = payload.range || payload;
    const range = lspRange(r);
    if (!range) throw new Error("Cannot rename this symbol");
    return payload.placeholder
      ? { range, placeholder: payload.placeholder }
      : range;
  },
  async provideRenameEdits(doc, position, newName) {
    if (!rpc || !rpc.ready) return;
    const payload = await request("textDocument/rename", {
      textDocument: { uri: doc.uri.toString() },
      position: { line: position.line, character: position.character },
      newName,
    });
    if (!payload) return;
    return lspWorkspaceEdit(payload);
  },
};

const codeActionProvider = {
  async provideCodeActions(doc, range, context) {
    if (!rpc || !rpc.ready) return;
    const diags = ((context && context.diagnostics) || []).map((d) => ({
      range: {
        start: { line: d.range.start.line, character: d.range.start.character },
        end: { line: d.range.end.line, character: d.range.end.character },
      },
      message: d.message,
      severity:
        d.severity === vscode.DiagnosticSeverity.Warning ? 2 : 1,
    }));
    const payload = await request("textDocument/codeAction", {
      textDocument: { uri: doc.uri.toString() },
      range: {
        start: { line: range.start.line, character: range.start.character },
        end: { line: range.end.line, character: range.end.character },
      },
      context: { diagnostics: diags },
    });
    if (!payload) return;
    const raw = Array.isArray(payload) ? payload : [];
    return raw
      .filter((a) => a && a.title && a.edit)
      .map((a) => {
        const item = new vscode.CodeAction(
          a.title,
          a.kind === "quickfix"
            ? vscode.CodeActionKind.QuickFix
            : vscode.CodeActionKind.QuickFix
        );
        item.edit = lspWorkspaceEdit(a.edit);
        item.isPreferred = !!a.isPreferred;
        return item;
      });
  },
};

const codeLensProvider = {
  async provideCodeLenses(doc) {
    if (!rpc || !rpc.ready) return [];
    const payload = await request("textDocument/codeLens", {
      textDocument: { uri: doc.uri.toString() },
    });
    if (!payload) return [];
    const raw = Array.isArray(payload) ? payload : [];
    return raw
      .filter((l) => l && l.range)
      .map((l) => {
        const cmd = l.command || {};
        return new vscode.CodeLens(lspRange(l.range), {
          title: cmd.title || "Run",
          command: cmd.command,
          arguments: cmd.arguments || [doc.uri.toString()],
        });
      });
  },
};

function activate(context) {
  diagnostics = vscode.languages.createDiagnosticCollection("rosegoldc");
  context.subscriptions.push(diagnostics);

  statusBar = vscode.window.createStatusBarItem(
    vscode.StatusBarAlignment.Right,
    100
  );
  statusBar.text = "$(loading~spin) RoseGoldC";
  statusBar.tooltip = "RoseGoldC language status";
  statusBar.command = "rosegoldc.recheck";
  statusBar.show();
  context.subscriptions.push(statusBar);

  startLanguageServer();

  context.subscriptions.push(
    vscode.commands.registerCommand("rosegoldc.runFile", (uri) =>
      runCli("run", uri)
    ),
    vscode.commands.registerCommand("rosegoldc.testFile", (uri) =>
      runCli("test", uri)
    ),
    vscode.commands.registerCommand("rosegoldc.recheck", () => {
      const ed = vscode.window.activeTextEditor;
      if (ed && ed.document.languageId === "rosegold")
        refreshDiagnostics(ed.document);
    }),
    vscode.workspace.onDidChangeTextDocument((e) =>
      scheduleDiagnostics(e.document)
    ),
    vscode.workspace.onDidOpenTextDocument((doc) => didOpen(doc)),
    vscode.workspace.onDidSaveTextDocument((doc) => refreshDiagnostics(doc)),
    vscode.workspace.onDidCloseTextDocument((doc) => {
      didClose(doc);
      diagnostics.delete(doc.uri);
    }),
    vscode.languages.registerDefinitionProvider("rosegold", definitionProvider),
    vscode.languages.registerHoverProvider("rosegold", hoverProvider),
    vscode.languages.registerSignatureHelpProvider(
      "rosegold",
      signatureHelpProvider,
      "(",
      ","
    ),
    vscode.languages.registerReferenceProvider("rosegold", referenceProvider),
    vscode.languages.registerCompletionItemProvider(
      "rosegold",
      completionProvider,
      ".",
      "@",
      ":"
    ),
    vscode.languages.registerDocumentSymbolProvider(
      "rosegold",
      documentSymbolProvider
    ),
    vscode.languages.registerDocumentHighlightProvider(
      "rosegold",
      documentHighlightProvider
    ),
    vscode.languages.registerRenameProvider("rosegold", renameProvider),
    vscode.languages.registerCodeActionsProvider(
      "rosegold",
      codeActionProvider,
      { providedCodeActionKinds: [vscode.CodeActionKind.QuickFix] }
    ),
    vscode.languages.registerCodeLensProvider("rosegold", codeLensProvider)
  );
}

async function deactivate() {
  await stopLanguageServer();
}

module.exports = { activate, deactivate };
