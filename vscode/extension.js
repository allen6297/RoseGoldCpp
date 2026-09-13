const vscode = require("vscode");
const { spawn } = require("child_process");
const fs = require("fs");
const os = require("os");
const path = require("path");

/** @type {vscode.DiagnosticCollection} */
let diagnostics;
/** @type {NodeJS.Timeout | undefined} */
let diagTimer;
/** @type {vscode.StatusBarItem} */
let statusBar;
/** @type {vscode.OutputChannel | undefined} */
let lspLog;
/** @type {{ child: import("child_process").ChildProcess, nextId: number, pending: Map<number, {resolve: Function, reject: Function}>, buf: Buffer, ready: boolean, cliPath: string } | null} */
let rpc = null;
let lspStopping = false;
let lspStartGen = 0;
/** @type {Promise<void> | null} */
let lspStartLock = null;

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

function pickCanonicalCli(paths) {
  if (!paths.length) return null;
  const pick = (name) =>
    paths.find((p) => path.basename(p).toLowerCase() === name.toLowerCase());
  return (
    pick("RoseGoldC.exe") ||
    pick("RoseGoldC") ||
    pick("RoseGoldC.stage.exe") ||
    newest(paths)
  );
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
  const hit = pickCanonicalCli(found);
  if (hit) return hit;

  return process.platform === "win32" ? "RoseGoldC.exe" : "RoseGoldC";
}

/** Copy CLI to a unique temp path so the LSP does not lock build/RoseGoldC.exe. */
function lspCliPath(source) {
  if (!path.isAbsolute(source)) return source;
  const cacheDir = path.join(os.tmpdir(), "rosegoldc-lsp");
  const stamp = `${process.pid}-${Date.now()}-${Math.floor(Math.random() * 1e6)}`;
  const dest = path.join(cacheDir, `RoseGoldC-${stamp}.exe`);
  try {
    fs.mkdirSync(cacheDir, { recursive: true });
    fs.copyFileSync(source, dest);
    // Drop older copies that are no longer in use (best-effort).
    try {
      for (const name of fs.readdirSync(cacheDir)) {
        if (!/^RoseGoldC-.*\.exe$/i.test(name)) continue;
        const full = path.join(cacheDir, name);
        if (full === dest) continue;
        try {
          fs.unlinkSync(full);
        } catch (_) {
          /* still running */
        }
      }
    } catch (_) {
      /* ignore cleanup */
    }
    return dest;
  } catch (err) {
    if (lspLog) lspLog.appendLine(`lsp cache: ${err}; using ${source}`);
    return source;
  }
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

function spawnCapture(cli, args, cwd) {
  return new Promise((resolve, reject) => {
    const child = spawn(cli, args, {
      cwd: cwd || undefined,
      env: process.env,
      windowsHide: true,
    });
    let stdout = "";
    let stderr = "";
    child.stdout.on("data", (d) => {
      stdout += d.toString();
    });
    child.stderr.on("data", (d) => {
      stderr += d.toString();
    });
    child.on("error", reject);
    child.on("close", (code) => {
      if (code === 0) resolve(stdout);
      else
        reject(
          new Error(
            (stderr || stdout || `fmt exited with code ${code}`).trim()
          )
        );
    });
  });
}

function workspaceRoot(hintPath) {
  if (hintPath) {
    const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(hintPath));
    if (folder) return folder.uri.fsPath;
  }
  if (vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders.length) {
    return vscode.workspace.workspaceFolders[0].uri.fsPath;
  }
  return hintPath ? path.dirname(hintPath) : process.cwd();
}

async function formatDocumentText(doc) {
  const file = filePathOf(doc);
  const cli = resolveCli(file);
  if (path.isAbsolute(cli) && !fs.existsSync(cli)) {
    throw new Error(
      `RoseGoldC.exe not found at ${cli}. Build with Ctrl+Shift+B, or set RoseGoldC › Cli Path.`
    );
  }
  const cfg = vscode.workspace.getConfiguration("rosegoldc");
  const args = ["fmt"];
  if (cfg.get("format.compact", false)) args.push("--compact");
  if (cfg.get("format.stripComments", false)) args.push("--no-comments");

  const tmp = path.join(
    os.tmpdir(),
    `rosegoldc-fmt-${process.pid}-${Date.now()}.rg`
  );
  fs.writeFileSync(tmp, doc.getText(), "utf8");
  try {
    args.push(tmp);
    return await spawnCapture(cli, args, workspaceCwd(file));
  } finally {
    try {
      fs.unlinkSync(tmp);
    } catch (_) {
      /* ignore */
    }
  }
}

/** @type {boolean} */
let fmtNeedsFormat = false;

async function checkFormatStatus(doc) {
  if (!doc || doc.languageId !== "rosegold") return;
  const cfg = vscode.workspace.getConfiguration("rosegoldc");
  if (!cfg.get("format.checkOnSave", true)) return;
  const file = filePathOf(doc);
  if (!file || doc.uri.scheme !== "file") return;
  const cli = resolveCli(file);
  if (path.isAbsolute(cli) && !fs.existsSync(cli)) return;
  try {
    await spawnCapture(cli, ["fmt", "--check", file], workspaceCwd(file));
    fmtNeedsFormat = false;
    updateStatus(diagnostics.get(doc.uri) || []);
  } catch (err) {
    const msg = String((err && err.message) || err);
    if (/needs formatting/i.test(msg)) {
      fmtNeedsFormat = true;
      if (statusBar) {
        statusBar.text = "$(warning) RoseGoldC fmt";
        statusBar.tooltip = "needs formatting";
      }
    }
  }
}

const documentFormattingProvider = {
  async provideDocumentFormattingEdits(doc) {
    try {
      if (rpc && rpc.ready) {
        try {
          const payload = await request("textDocument/formatting", {
            textDocument: { uri: doc.uri.toString() },
            options: { tabSize: 4, insertSpaces: true },
          });
          if (payload) {
            const raw = Array.isArray(payload) ? payload : [];
            const edits = raw
              .filter((e) => e && e.range)
              .map(
                (e) =>
                  new vscode.TextEdit(lspRange(e.range), e.newText || "")
              );
            fmtNeedsFormat = false;
            updateStatus(diagnostics.get(doc.uri) || []);
            return edits;
          }
        } catch (_) {
          /* fall through to spawn fmt */
        }
      }
      const formatted = await formatDocumentText(doc);
      const current = doc.getText();
      fmtNeedsFormat = false;
      updateStatus(diagnostics.get(doc.uri) || []);
      if (formatted === current) return [];
      const full = new vscode.Range(
        doc.positionAt(0),
        doc.positionAt(current.length)
      );
      return [vscode.TextEdit.replace(full, formatted)];
    } catch (err) {
      vscode.window.showErrorMessage(
        `RoseGoldC format failed: ${err.message || err}`
      );
      return [];
    }
  },
};

async function ensureCli(cli) {
  const abs = path.isAbsolute(cli) ? cli : cli;
  if (path.isAbsolute(abs) && !fs.existsSync(abs)) {
    const pick = await vscode.window.showErrorMessage(
      `RoseGoldC.exe not found at ${abs}. Build with Ctrl+Shift+B, or set RoseGoldC › Cli Path.`,
      "Build"
    );
    if (pick === "Build") {
      await vscode.commands.executeCommand("workbench.action.tasks.build");
    }
    return false;
  }
  return true;
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

  // Explicit null = full test suite (no file argument).
  if (file === null && subcommand === "test") {
    const cwd = workspaceRoot();
    const cli = resolveCli(cwd);
    if (!(await ensureCli(cli))) return;
    const def = { type: "rosegoldc", task: "test-suite" };
    const exec = new vscode.ShellExecution(cli, ["test"], { cwd });
    const task = new vscode.Task(
      def,
      vscode.TaskScope.Workspace,
      "RoseGoldC test suite",
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
    return;
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
  if (!(await ensureCli(cli))) return;

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
  } else if (fmtNeedsFormat) {
    statusBar.text = "$(warning) RoseGoldC fmt";
    statusBar.tooltip = "needs formatting";
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
  if (lspStartLock) return lspStartLock;
  const gen = ++lspStartGen;
  lspStartLock = (async () => {
    try {
      if (rpc) await stopLanguageServer();
      if (gen !== lspStartGen) return;

      const hint =
        (vscode.workspace.workspaceFolders &&
          vscode.workspace.workspaceFolders[0] &&
          vscode.workspace.workspaceFolders[0].uri.fsPath) ||
        "";
      const source = resolveCli(hint);
      const cli = lspCliPath(source);
      if (!lspLog) lspLog = vscode.window.createOutputChannel("RoseGoldC LSP");
      lspLog.appendLine(`starting ${cli} lsp (from ${source})`);
      lspStopping = false;

      const child = spawn(cli, ["lsp"], {
        cwd: hint || undefined,
        env: process.env,
        stdio: ["pipe", "pipe", "pipe"],
        windowsHide: true,
      });
      rpc = {
        child,
        nextId: 1,
        pending: new Map(),
        buf: Buffer.alloc(0),
        ready: false,
        cliPath: cli,
      };

      child.stdout.on("data", (chunk) => {
        if (!rpc || rpc.child !== child) return;
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
      child.on("close", (code, signal) => {
        if (rpc && rpc.child === child) {
          rpc.ready = false;
          rpc = null;
        }
        const why = signal ? `signal ${signal}` : `code ${code}`;
        lspLog.appendLine(`lsp exited (${why})`);
        if (cli.includes("rosegoldc-lsp")) {
          try {
            fs.unlinkSync(cli);
          } catch (_) {
            /* ignore */
          }
        }
        if (lspStopping || gen !== lspStartGen) return;
        if (statusBar) {
          statusBar.text = "$(error) RoseGoldC";
          statusBar.tooltip = `language server exited (${why})`;
        }
      });

      const root =
        vscode.workspace.workspaceFolders &&
        vscode.workspace.workspaceFolders[0];
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
      await Promise.race([init, timeout]);
      if (!rpc || rpc.child !== child || gen !== lspStartGen) return;
      notify("initialized", {});
      rpc.ready = true;
      if (statusBar) {
        statusBar.text = "$(check) RoseGoldC";
        statusBar.tooltip = "Language server ready";
      }
      for (const doc of vscode.workspace.textDocuments) didOpen(doc);
    } catch (err) {
      if (statusBar) {
        statusBar.text = "$(error) RoseGoldC";
        statusBar.tooltip = String(err.message || err);
      }
      if (lspLog) lspLog.appendLine(String(err));
      if (rpc) {
        try {
          rpc.child.kill();
        } catch (_) {
          /* ignore */
        }
        rpc = null;
      }
    } finally {
      if (lspStartLock && gen === lspStartGen) lspStartLock = null;
    }
  })();
  return lspStartLock;
}

async function stopLanguageServer() {
  if (!rpc) return;
  lspStopping = true;
  const child = rpc.child;
  const cliPath = rpc.cliPath;
  const closed = new Promise((resolve) => {
    child.once("close", () => resolve());
    setTimeout(resolve, 1500);
  });
  const shutdown = request("shutdown", null).catch(() => {});
  try {
    await Promise.race([
      shutdown.then(() => notify("exit", undefined)),
      new Promise((resolve) => setTimeout(resolve, 500)),
    ]);
  } catch (_) {
    /* ignore */
  }
  if (rpc && rpc.child === child) {
    rpc.ready = false;
  }
  try {
    child.kill();
  } catch (_) {
    /* ignore */
  }
  await closed;
  if (rpc && rpc.child === child) rpc = null;
  if (cliPath && cliPath.includes("rosegoldc-lsp")) {
    try {
      fs.unlinkSync(cliPath);
    } catch (_) {
      /* ignore */
    }
  }
}

async function restartLanguageServer() {
  await stopLanguageServer();
  lspStopping = false;
  await startLanguageServer();
  const ed = vscode.window.activeTextEditor;
  if (ed && ed.document.languageId === "rosegold") {
    refreshDiagnostics(ed.document);
  }
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

const documentColorProvider = {
  async provideDocumentColors(doc) {
    if (!rpc || !rpc.ready) return [];
    const payload = await request("textDocument/documentColor", {
      textDocument: { uri: doc.uri.toString() },
    });
    if (!payload) return [];
    const raw = Array.isArray(payload) ? payload : [];
    return raw
      .filter((c) => c && c.range && c.color)
      .map((c) => {
        const col = c.color;
        return new vscode.ColorInformation(
          lspRange(c.range),
          new vscode.Color(
            Number(col.red) || 0,
            Number(col.green) || 0,
            Number(col.blue) || 0,
            col.alpha == null ? 1 : Number(col.alpha)
          )
        );
      });
  },
  async provideColorPresentations(color, context) {
    if (!rpc || !rpc.ready) return [];
    const range = context.range;
    const payload = await request("textDocument/colorPresentation", {
      textDocument: { uri: context.document.uri.toString() },
      color: {
        red: color.red,
        green: color.green,
        blue: color.blue,
        alpha: color.alpha,
      },
      range: {
        start: { line: range.start.line, character: range.start.character },
        end: { line: range.end.line, character: range.end.character },
      },
    });
    if (!payload) return [];
    const raw = Array.isArray(payload) ? payload : [];
    return raw
      .filter((p) => p && p.label)
      .map((p) => {
        const item = new vscode.ColorPresentation(p.label);
        if (p.textEdit && p.textEdit.range)
          item.textEdit = new vscode.TextEdit(
            lspRange(p.textEdit.range),
            p.textEdit.newText || p.label
          );
        return item;
      });
  },
};

const foldingRangeProvider = {
  async provideFoldingRanges(doc) {
    if (!rpc || !rpc.ready) return;
    const payload = await request("textDocument/foldingRange", {
      textDocument: { uri: doc.uri.toString() },
    });
    if (!payload) return;
    const raw = Array.isArray(payload) ? payload : [];
    return raw
      .filter((r) => r && r.startLine != null && r.endLine != null)
      .map(
        (r) =>
          new vscode.FoldingRange(
            r.startLine,
            r.endLine,
            r.kind === "comment"
              ? vscode.FoldingRangeKind.Comment
              : r.kind === "imports"
                ? vscode.FoldingRangeKind.Imports
                : vscode.FoldingRangeKind.Region
          )
      );
  },
};

const semanticTokensLegend = new vscode.SemanticTokensLegend(
  [
    "keyword",
    "comment",
    "string",
    "number",
    "function",
    "type",
    "variable",
    "operator",
    "namespace",
    "method",
    "property",
  ],
  ["declaration", "deprecated"]
);

const documentSemanticTokensProvider = {
  async provideDocumentSemanticTokens(doc) {
    if (!rpc || !rpc.ready) return;
    const payload = await request("textDocument/semanticTokens/full", {
      textDocument: { uri: doc.uri.toString() },
    });
    if (!payload || !payload.data) return;
    return new vscode.SemanticTokens(Uint32Array.from(payload.data));
  },
};

const inlayHintsProvider = {
  async provideInlayHints(doc, range) {
    if (!rpc || !rpc.ready) return;
    const payload = await request("textDocument/inlayHint", {
      textDocument: { uri: doc.uri.toString() },
      range: {
        start: { line: range.start.line, character: range.start.character },
        end: { line: range.end.line, character: range.end.character },
      },
    });
    if (!payload) return;
    const raw = Array.isArray(payload) ? payload : [];
    return raw
      .filter((h) => h && h.position)
      .map((h) => {
        const label =
          typeof h.label === "string"
            ? h.label
            : Array.isArray(h.label)
              ? h.label.map((p) => (typeof p === "string" ? p : p.value || "")).join("")
              : String(h.label || "");
        const kind =
          h.kind === 1
            ? vscode.InlayHintKind.Type
            : h.kind === 2
              ? vscode.InlayHintKind.Parameter
              : undefined;
        const hint = new vscode.InlayHint(
          new vscode.Position(
            Math.max(0, h.position.line || 0),
            Math.max(0, h.position.character || 0)
          ),
          label,
          kind
        );
        if (h.paddingLeft) hint.paddingLeft = true;
        if (h.paddingRight) hint.paddingRight = true;
        return hint;
      });
  },
};

function makeShellTask(def, name, args, cwd) {
  const exec = new vscode.ShellExecution(args[0], args.slice(1), { cwd });
  const task = new vscode.Task(
    def,
    vscode.TaskScope.Workspace,
    name,
    "rosegoldc",
    exec
  );
  task.presentationOptions = {
    reveal: vscode.TaskRevealKind.Always,
    panel: vscode.TaskPanelKind.Dedicated,
    clear: true,
  };
  task.problemMatchers = ["$rosegoldc", "$rosegoldc-located"];
  return task;
}

const rosegoldTaskProvider = {
  provideTasks() {
    const cwd = workspaceRoot();
    const cli = resolveCli(cwd);
    const build = new vscode.Task(
      { type: "rosegoldc", task: "build" },
      vscode.TaskScope.Workspace,
      "build",
      "rosegoldc",
      new vscode.ShellExecution(
        "powershell",
        [
          "-NoProfile",
          "-ExecutionPolicy",
          "Bypass",
          "-File",
          path.join(cwd, "build.ps1"),
        ],
        { cwd }
      )
    );
    build.group = vscode.TaskGroup.Build;
    build.problemMatchers = ["$rosegoldc", "$rosegoldc-located"];
    build.presentationOptions = {
      reveal: vscode.TaskRevealKind.Always,
      panel: vscode.TaskPanelKind.Dedicated,
      clear: true,
    };

    const suite = makeShellTask(
      { type: "rosegoldc", task: "test-suite" },
      "RoseGoldC: Test Suite",
      [cli, "test"],
      cwd
    );

    const ed = vscode.window.activeTextEditor;
    const file =
      ed && ed.document.languageId === "rosegold"
        ? filePathOf(ed.document)
        : null;
    const runArgs = file ? [cli, "run", file] : [cli, "run"];
    const run = makeShellTask(
      { type: "rosegoldc", task: "run" },
      "RoseGoldC: Run Current File",
      runArgs,
      file ? workspaceCwd(file) : cwd
    );

    return [build, suite, run];
  },
  resolveTask(task) {
    return task;
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
    vscode.commands.registerCommand("rosegoldc.testSuite", () =>
      runCli("test", null)
    ),
    vscode.commands.registerCommand("rosegoldc.recheck", () => {
      const ed = vscode.window.activeTextEditor;
      if (ed && ed.document.languageId === "rosegold")
        refreshDiagnostics(ed.document);
    }),
    vscode.commands.registerCommand("rosegoldc.restartLsp", () =>
      restartLanguageServer()
    ),
    vscode.commands.registerCommand("rosegoldc.formatDocument", async () => {
      const ed = vscode.window.activeTextEditor;
      if (!ed || ed.document.languageId !== "rosegold") {
        vscode.window.showErrorMessage("Open a .rg file first.");
        return;
      }
      await vscode.commands.executeCommand("editor.action.formatDocument");
    }),
    vscode.commands.registerCommand("rosegoldc.debugFile", async (uri) => {
      let file = null;
      if (uri && uri.fsPath) file = uri.fsPath;
      else {
        const ed = vscode.window.activeTextEditor;
        if (ed && ed.document.languageId === "rosegold") {
          if (ed.document.isDirty) await ed.document.save();
          file = ed.document.uri.fsPath;
        }
      }
      if (!file) {
        vscode.window.showErrorMessage("Open a .rg file first.");
        return;
      }
      const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(file));
      await vscode.debug.startDebugging(folder, {
        type: "rosegoldc",
        request: "launch",
        name: "RoseGoldC: Debug",
        program: file,
        cwd: folder ? folder.uri.fsPath : path.dirname(file),
        stopOnEntry: true,
      });
    }),
    vscode.debug.registerDebugConfigurationProvider("rosegoldc", {
      provideDebugConfigurations() {
        return [
          {
            type: "rosegoldc",
            request: "launch",
            name: "RoseGoldC: Debug current file",
            program: "${file}",
            cwd: "${workspaceFolder}",
            stopOnEntry: true,
          },
        ];
      },
      resolveDebugConfiguration(_folder, config) {
        if (!config.type && !config.request && !config.name) {
          const ed = vscode.window.activeTextEditor;
          if (ed && ed.document.languageId === "rosegold") {
            config.type = "rosegoldc";
            config.request = "launch";
            config.name = "RoseGoldC: Debug";
            config.program = ed.document.uri.fsPath;
            config.cwd = workspaceCwd(ed.document.uri.fsPath);
            config.stopOnEntry = true;
          }
        }
        if (config.request === "launch" && !config.program) {
          const ed = vscode.window.activeTextEditor;
          if (ed && ed.document.languageId === "rosegold")
            config.program = ed.document.uri.fsPath;
        }
        if (!config.cwd && config.program)
          config.cwd = workspaceCwd(config.program);
        if (config.stopOnEntry == null) config.stopOnEntry = true;
        if (!config.program) {
          vscode.window.showErrorMessage(
            "RoseGoldC debug: set program to a .rg file (or open one and press F5)."
          );
          return undefined;
        }
        return config;
      },
    }),
    vscode.languages.registerDocumentFormattingEditProvider(
      "rosegold",
      documentFormattingProvider
    ),
    vscode.workspace.onDidChangeTextDocument((e) =>
      scheduleDiagnostics(e.document)
    ),
    vscode.workspace.onDidOpenTextDocument((doc) => didOpen(doc)),
    vscode.workspace.onDidSaveTextDocument((doc) => {
      refreshDiagnostics(doc);
      checkFormatStatus(doc);
    }),
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
    vscode.languages.registerCodeLensProvider("rosegold", codeLensProvider),
    vscode.languages.registerColorProvider("rosegold", documentColorProvider),
    vscode.languages.registerFoldingRangeProvider(
      "rosegold",
      foldingRangeProvider
    ),
    vscode.languages.registerDocumentSemanticTokensProvider(
      "rosegold",
      documentSemanticTokensProvider,
      semanticTokensLegend
    ),
    vscode.languages.registerInlayHintsProvider("rosegold", inlayHintsProvider),
    vscode.tasks.registerTaskProvider("rosegoldc", rosegoldTaskProvider),
    vscode.debug.registerDebugAdapterDescriptorFactory("rosegoldc", {
      createDebugAdapterDescriptor() {
        const hint =
          (vscode.workspace.workspaceFolders &&
            vscode.workspace.workspaceFolders[0] &&
            vscode.workspace.workspaceFolders[0].uri.fsPath) ||
          "";
        const cli = resolveCli(hint);
        return new vscode.DebugAdapterExecutable(cli, ["dap"], {
          cwd: hint || undefined,
        });
      },
    })
  );
}

async function deactivate() {
  await stopLanguageServer();
}

module.exports = { activate, deactivate };
