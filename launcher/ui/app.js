/* LUAp0rt Launcher -- dashboard client. Vanilla JS, talks JSON to the local
   Python backend. Nothing here reaches the console directly. */
(function () {
  "use strict";

  const $ = (id) => document.getElementById(id);
  const esc = (s) => String(s == null ? "" : s).replace(/[&<>"']/g, (c) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

  const state = {
    data: null,
    logNext: 0,
    jobNext: 0,
    jobRunning: false,
    activeTab: "console",
    pingBusy: false,
    failCount: 0,
    stopped: false,
  };

  // ------------------------------------------------------------ HTTP
  async function api(path, body) {
    const opts = body === undefined ? {} :
      { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) };
    const r = await fetch(path, opts);
    let j = null;
    try { j = await r.json(); } catch (e) { j = { error: "bad response" }; }
    if (!r.ok || j.error) { const err = new Error(j.error || ("HTTP " + r.status)); err.code = j.code; throw err; }
    return j;
  }

  let toastTimer = null;
  function toast(msg, isErr) {
    const t = $("toast");
    t.textContent = msg;
    t.classList.toggle("err", !!isErr);
    t.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { t.hidden = true; }, isErr ? 7000 : 3500);
  }

  // ------------------------------------------------------------ server gone
  function noteFailure() {
    state.failCount++;
    if (state.failCount >= 4 && !state.stopped) serverGone("The launcher server has stopped.");
  }

  function noteSuccess() { state.failCount = 0; }

  function serverGone(msg) {
    if (state.stopped) return;
    state.stopped = true;
    const box = document.createElement("div");
    box.className = "stopped";
    box.innerHTML = '<div class="box"><h1>Launcher stopped</h1><p class="muted">' + esc(msg || "") +
      ' This tab closes itself; if your browser keeps it open, just close it. Run LUAp0rt-Launcher again to reopen the dashboard.</p></div>';
    document.body.appendChild(box);
    // Browsers allow a page to close its own tab only when the tab was opened
    // straight to this page (which is how the launcher opens it). Try, and
    // leave the message above if the browser refuses.
    setTimeout(() => { try { window.close(); } catch (e) { /* refused */ } }, 400);
    setTimeout(() => { try { window.open("", "_self"); window.close(); } catch (e) { /* refused */ } }, 900);
  }

  // ------------------------------------------------------------ state
  async function refresh(full) {
    if (state.stopped) return null;
    try {
      const d = await api("/api/state");
      noteSuccess();
      state.data = d;
      render();
      return d;
    } catch (e) {
      setPill("bad", "Launcher backend unreachable");
      noteFailure();
      return null;
    }
  }

  async function saveConfig(patch) {
    try {
      const r = await api("/api/config", patch);
      state.data = r.state;
      render();
    } catch (e) {
      toast(e.message, true);
      refresh(true);
    }
  }

  // ------------------------------------------------------------ render
  function ico(kind) {
    const map = { good: "●", warn: "▲", bad: "✕", info: "●", muted: "○" };
    return '<span class="ico ico-' + kind + '">' + (map[kind] || "○") + "</span>";
  }

  function tile(id, kind, value, sub) {
    const t = $(id);
    t.className = "tile tile-" + kind;
    t.querySelector(".tile-value").innerHTML = ico(kind) + " " + esc(value);
    t.querySelector(".tile-sub").textContent = sub;
    t.querySelector(".tile-sub").title = sub;
  }

  function setPill(kind, text) {
    const p = $("console-pill");
    p.className = "pill pill-" + kind;
    p.querySelector(".pill-text").textContent = text;
  }

  function fmtAge(sec) {
    if (sec == null) return null;
    if (sec < 2) return "just now";
    if (sec < 60) return Math.round(sec) + "s ago";
    if (sec < 3600) return Math.round(sec / 60) + " min ago";
    return Math.round(sec / 3600) + " h ago";
  }

  function render() {
    try { renderInner(); }
    catch (e) {
      console.error("render failed", e);
      if (!state.renderErrorShown) { state.renderErrorShown = true; toast("Dashboard rendering error: " + e.message + " (see browser console)", true); }
    }
  }

  function renderInner() {
    const d = state.data;
    if (!d) return;
    const cfg = d.config;

    // inputs (don't clobber while the user is typing)
    syncInput("ip", cfg.ps5_ip);
    syncInput("rom-dir", cfg.rom_dir);
    syncInput("bios-path", cfg.bios_path);
    $("allow-unverified").checked = !!cfg.allow_unverified;
    $("foot-ver").textContent = "v" + d.app.version;

    // ---- console tile / pill
    const c = d.console;
    const logAge = c.log.last_rx_age;
    const pingOk = c.ping && c.ping.ok;
    const pingAge = c.ping && c.ping.when ? (Date.now() / 1000 - c.ping.when) : null;
    if (!c.ip_valid) {
      tile("tile-console", "muted", "No address", "enter the PS5 IP in the top bar");
      setPill("muted", "Console: no address");
    } else if (logAge != null && logAge < 30) {
      tile("tile-console", "good", "Payload running", "log from " + c.log.last_from + " " + fmtAge(logAge));
      setPill("good", "Console: payload logging (" + fmtAge(logAge) + ")");
    } else if (pingOk && pingAge != null && pingAge < 3 * c.auto_ping) {
      const ldl = c.loader && c.loader.state === "ok" ? " · loader listening" : (c.loader && c.loader.state === "bad" ? " · loader NOT listening" : "");
      tile("tile-console", c.loader && c.loader.state === "bad" ? "warn" : "good", "Reachable", c.ip + " · ping " + c.ping.ms + " ms" + ldl);
      setPill("good", "Console: reachable");
    } else if (c.ping && c.ping.ok === false && pingAge != null && pingAge < 3 * c.auto_ping) {
      tile("tile-console", "bad", "No ping reply", c.ip + " did not answer · retrying every " + c.auto_ping + " s");
      setPill("bad", "Console: no ping reply");
    } else {
      const sub = logAge != null ? "last log line " + fmtAge(logAge) : "checking " + c.ip + "…";
      tile("tile-console", "warn", "Checking", sub);
      setPill("warn", "Console: checking " + c.ip);
    }

    // ---- payload tile + list
    const p = d.payload;
    if (!p) {
      tile("tile-payload", "bad", "Not found", "no m16cgpsp.lua + .bin pair found");
    } else if (!p.ok) {
      tile("tile-payload", "bad", "Incomplete", p.problems.join("; "));
    } else if (p.verified) {
      tile("tile-payload", "good", "Verified v0.0.1", p.bin_rel + " · SHA-256 matches golden");
    } else {
      tile("tile-payload", cfg.allow_unverified ? "warn" : "bad", "Unverified",
        p.bin_rel + " · SHA-256 differs from the frozen release");
    }
    renderPayloads(d);

    // ---- BIOS
    const b = d.bios;
    const biosKind = b.ok ? "good" : (b.state === "unset" ? "muted" : "bad");
    tile("tile-bios", biosKind, b.ok ? "Size OK" : (b.state === "unset" ? "Not set" : "Problem"),
      b.ok ? "FNV-1a " + b.fnv1a : b.message);
    $("bios-status").innerHTML = b.path ? (ico(biosKind) + "<span>" + esc(b.message) +
      (b.ok ? " · <code>fnv1a=" + esc(b.fnv1a) + "</code>" : "") + "</span>") : "";

    // ---- upload availability
    const up = d.tools.upload;
    const bs = b.send || {};
    const sentAt = bs.sent_at ? new Date(bs.sent_at * 1000).toLocaleTimeString([], { hour12: false, hour: "2-digit", minute: "2-digit" }) : "";
    $("bios-send").innerHTML = !b.ok ? 'Once verified, it goes to <code>' + esc(d.facts.console_bios_path) + '</code> with the next launch if the console does not have it yet.' :
      bs.needs_send ? '<span class="ico ico-info">●</span> Will be sent to <code>' + esc(d.facts.console_bios_path) + '</code> with the next launch (' + esc(bs.why) + ').' :
      '<span class="ico ico-good">●</span> On the console: ' + esc(bs.why) + (sentAt ? " (sent " + esc(sentAt) + ")" : "") + '. Not sent again unless the console reports it missing.';
    $("upload-unavailable").hidden = !!(up && up.available);
    if (up && !up.available) {
      $("upload-unavailable").textContent = "File upload is not available here: " + up.reason + ".";
    }

    // ---- library
    renderLibrary(d);
    const lib = d.roms;
    $("btn-check-all").disabled = $("btn-uncheck-all").disabled = !(lib && lib.ok && lib.count > 0);
    const lr = $("launch-roms");
    if (lib && lib.ok && lib.count > 0) {
      lr.hidden = false;
      const biosFirst = d.bios && d.bios.send && d.bios.send.needs_send;
      lr.innerHTML = "<b>" + lib.checked + "</b> of " + lib.count + " game" + (lib.count === 1 ? "" : "s") + " checked · " +
        (lib.to_send > 0 || biosFirst ? "<b>" + (biosFirst ? "the BIOS" + (lib.to_send > 0 ? " + " : "") : "") + (lib.to_send > 0 ? lib.to_send + " game" + (lib.to_send === 1 ? "" : "s") : "") + "</b> will be sent to the console first" : "nothing to send: the BIOS and the checked games are already on the console") +
        (lib.checked > lib.to_send && lib.to_send >= 0 ? ' <span class="muted">(games already sent unchanged, or reported present, are skipped)</span>' : "");
    } else { lr.hidden = true; }
    const biosGo = d.bios && d.bios.send && d.bios.send.needs_send;
    const nSend = lib && lib.ok ? lib.to_send : 0;
    $("btn-launch").querySelector(".btn-launch-text").textContent =
      state.jobRunning ? "Working…" : (nSend > 0 || biosGo ? "Send " + (biosGo ? "BIOS" + (nSend > 0 ? " + " : "") : "") + (nSend > 0 ? nSend + " game" + (nSend === 1 ? "" : "s") : "") + " + Launch" : "Launch LUAp0rt GBA");

    // ---- checklist + launch button
    const ck = $("checklist");
    setCheck(ck, "ip", c.ip_valid ? "ok" : "bad");
    setCheck(ck, "payload", p && p.ok && (p.verified || cfg.allow_unverified) ? "ok" : "bad");
    const ld = c.loader || { state: "manual", text: "" };
    const ldCls = { ok: "ok", bad: "bad", hold: "hold", stale: "stale", off: "manual" }[ld.state] || "manual";
    setCheck(ck, "loader", ldCls);
    $("loader-text").textContent = ld.text ? "· " + ld.text : "";
    $("probe-loader").checked = cfg.probe_loader !== false;
    $("btn-probe").disabled = !c.ip_valid || state.jobRunning;
    const scan = d.console_scan || {};
    if (scan.when && !scan.failed) {
      // The payload logs its BIOS check only when the first game is loaded, so
      // "no BIOS lines yet" is unknown, not a failure. Only explicit evidence
      // (all candidates "not present", or SIZE WRONG) counts against it.
      const sb = scan.bios || {};
      const biosBad = sb.size_ok === false || (!sb.present && Array.isArray(sb.missing) && sb.missing.length > 0);
      const biosText = sb.present ? "BIOS found at " + sb.path + (sb.size_ok === false ? " but wrong size" : "") :
        (biosBad ? "BIOS not found at either console path" : "BIOS is checked by the console when the first game loads");
      setCheck(ck, "files", (scan.available > 0 && !biosBad) ? "ok" : "bad");
      ck.querySelector('[data-k="files"]').title = "From the console's own scan: " + scan.available + " playable ROM(s) · " + biosText;
    } else {
      setCheck(ck, "files", "manual");
      ck.querySelector('[data-k="files"]').title = "Not verified yet: the console reports this when the emulator starts";
    }
    $("btn-launch").disabled = !d.tools.send || state.jobRunning;
    $("btn-launch").classList.toggle("running", state.jobRunning);
    $("btn-cancel").hidden = !state.jobRunning;
    if (!d.tools.send) {
      $("launch-hint").innerHTML = '<span class="ico ico-bad">✕</span> tools/send.py was not found next to this launcher.';
    }

    // (the setup wizard re-renders itself only on its own actions, so typing is never interrupted)

    // ---- log bind error
    const be = c.log.bind_error;
    $("log-bind-error").hidden = !be;
    if (be) $("log-bind-error").textContent = "Console log unavailable: " + be + " (is nc or another launcher listening on 9027?)";
  }

  function syncInput(id, value) {
    const el = $(id);
    if (document.activeElement === el) return;
    if (el.value !== (value || "")) el.value = value || "";
  }

  function setCheck(list, key, cls) {
    const li = list.querySelector('[data-k="' + key + '"]');
    li.className = cls;
  }

  function renderPayloads(d) {
    const box = $("payload-list");
    const cands = d.payload_candidates || [];
    if (!cands.length) {
      box.innerHTML = '<div class="note note-warn">No payload pair found. Expected m16cgpsp.lua + m16cgpsp.bin in release/luap0rt-gba-v0.0.1/binary/ or build/m16c/.</div>';
      return;
    }
    const sel = d.payload ? d.payload.key : "";
    box.innerHTML = cands.map((c) => {
      const badge = !c.ok ? '<span class="badge badge-bad">✕ incomplete</span>' :
        c.verified ? '<span class="badge badge-good">✓ verified</span>' :
          '<span class="badge badge-warn">▲ unverified</span>';
      const detail = c.ok ?
        '<div class="p-hash">' + esc(c.bin_rel) + ' · ' + esc(String(c.bin_size).replace(/\B(?=(\d{3})+(?!\d))/g, ",")) + ' B · sha256 ' + esc(c.bin_sha256.slice(0, 16)) + '…</div>' :
        '<div class="p-problem">' + esc(c.problems.join("; ")) + '</div>';
      return '<label class="payload' + (c.key === sel ? " selected" : "") + '">' +
        '<input type="radio" name="payload" value="' + esc(c.key) + '"' + (c.key === sel ? " checked" : "") + '>' +
        '<div><div class="p-name">' + esc(c.label) + '</div>' + detail + '</div>' + badge + '</label>';
    }).join("");
    box.querySelectorAll('input[name="payload"]').forEach((r) => {
      r.addEventListener("change", () => saveConfig({ payload: r.value }));
    });
  }

  function renderLibrary(d) {
    const r = d.roms;
    const rows = $("rom-rows");
    const empty = $("rom-empty");
    const note = $("library-note");
    const filter = $("rom-filter").value.trim().toLowerCase();
    const up = d.tools.upload && d.tools.upload.available;
    if (!r || !r.ok) {
      rows.innerHTML = "";
      empty.hidden = false;
      empty.textContent = r ? r.message : "No ROM folder selected.";
      note.textContent = "";
      tile("tile-library", "muted", "No folder", "pick the folder that holds your .gba files");
      return;
    }
    const cap = r.capacity;
    const cs = r.console || {};
    const onConsole = r.roms.filter((g) => g.console && g.console.present).length;
    const kind = r.count === 0 ? "warn" : (r.over_capacity ? "warn" : "good");
    const scanTime = cs.when ? new Date(cs.when * 1000).toLocaleTimeString([], { hour12: false, hour: "2-digit", minute: "2-digit" }) : null;
    tile("tile-library", kind, r.count + " game" + (r.count === 1 ? "" : "s"),
      cs.have_scan && !cs.failed ? onConsole + " of " + r.count + " verified on console (scan " + scanTime + ")" :
      "console not verified yet · launch to check" + (r.over_capacity ? " · only the first " + cap + " fit the picker" : ""));
    const co = $("console-only");
    if (cs.have_scan && !cs.failed) {
      const extra = (r.console_only || []).map((f) => f.name + (f.size ? " (" + fmtBytes(f.size) + ")" : ""));
      co.hidden = false;
      co.innerHTML = "<b>Console scan " + scanTime + ":</b> " + cs.listed + " ROM" + (cs.listed === 1 ? "" : "s") + " in " + esc(d.facts.console_rom_dir) +
        ", " + cs.available + " accepted by the picker" +
        (cs.bios && cs.bios.present ? " · BIOS found at " + esc(cs.bios.path) + (cs.bios.size_ok === false ? " (wrong size)" : "") : (cs.bios && cs.bios.missing ? " · <span class=\"ico-bad\">BIOS not found</span>" : "")) +
        (extra.length ? "<br>Also on console but not in this folder: " + esc(extra.join(", ")) : "") +
        " · <span class=\"muted\">updates again each time LUAp0rt launches</span>";
    } else if (cs.failed) {
      co.hidden = false;
      co.textContent = "The payload reported that it could not enumerate " + d.facts.console_rom_dir + " at its last start.";
    } else {
      co.hidden = false;
      co.textContent = "On-console status is not verified yet. It updates automatically when you launch LUAp0rt: the console reports what is in " + d.facts.console_rom_dir + " each time the emulator starts. Nothing is assumed from earlier sessions.";
    }
    note.textContent = r.over_capacity ?
      "This folder has " + r.count + " ROMs, but the console picker lists at most " + cap + ". Keep /temp0 at " + cap + " or fewer." : "";

    const list = r.roms.filter((g) => !filter ||
      (g.name + " " + g.title + " " + g.code).toLowerCase().includes(filter));
    empty.hidden = list.length > 0;
    empty.textContent = r.count === 0 ? "No .gba files in this folder." : "No games match the filter.";
    rows.innerHTML = list.map((g, i) => {
      const idx = r.roms.indexOf(g);
      const sentAt = g.uploaded ? new Date(g.uploaded.when * 1000).toLocaleTimeString([], { hour12: false, hour: "2-digit", minute: "2-digit" }) : "";
      let sent = '<span class="badge" title="The console reports its /temp0 contents each time LUAp0rt launches; verified then.' + (g.upload_state === "current" ? " Sent by this launcher at " + esc(sentAt) + "." : "") + '">○ will verify</span>';
      if (g.console) {
        if (g.console.pending) sent = '<span class="badge" title="Sent at ' + esc(sentAt) + ', after the console last reported; verified at the next launch">○ will verify</span>';
        else if (!g.console.present) sent = '<span class="badge badge-bad" title="Not in the console\'s /temp0 report from this session">✕ verified: not on console</span>';
        else if (g.console.ok === false) sent = '<span class="badge badge-warn" title="On the console, but the picker rejects it">▲ verified: ' + esc(g.console.reason) + '</span>';
        else if (!g.console.size_match) sent = '<span class="badge badge-warn" title="Console copy is ' + fmtBytes(g.console.size) + '">▲ verified: different size</span>';
        else sent = '<span class="badge badge-good" title="Listed by the console\'s own report' + (g.console.size != null ? ", " + fmtBytes(g.console.size) : "") + '">✓ verified</span>';
      }
      return '<tr class="' + (idx >= cap ? "over" : "") + (g.checked ? "" : " unchecked") + '"' + (idx >= cap ? ' title="Beyond the picker capacity"' : "") + '>' +
        '<td class="sel"><input type="checkbox" data-sel="' + esc(g.name) + '"' + (g.checked ? " checked" : "") + ' title="Include with launch"></td>' +
        '<td class="title">' + esc(g.title || "(no internal title)") + '</td>' +
        '<td class="file" title="' + esc(g.path) + '">' + esc(g.name) + '</td>' +
        '<td class="num">' + esc(g.size_h) + '</td>' +
        '<td class="code">' + esc(g.code || "") + '</td>' +
        '<td>' + sent + '</td></tr>';
    }).join("");
    rows.querySelectorAll("input[data-sel]").forEach((cb) => {
      cb.addEventListener("change", () => setChecked([cb.dataset.sel], cb.checked));
    });
    const all = $("sel-all");
    all.checked = r.count > 0 && r.checked === r.count;
    all.indeterminate = r.checked > 0 && r.checked < r.count;
  }

  // ------------------------------------------------------------ selection
  function setChecked(names, checked) {
    const d = state.data;
    if (!d || !d.roms || !d.roms.ok) return;
    const desel = new Set(d.roms.roms.filter((g) => !g.checked).map((g) => g.name));
    names.forEach((n) => { if (checked) desel.delete(n); else desel.add(n); });
    saveConfig({ deselected: Array.from(desel) });
  }

  // ------------------------------------------------------------ jobs
  function launchChecks() {
    const d = state.data, cfg = d.config, c = d.console, p = d.payload;
    const ck = $("checklist");
    const cls = (k) => ck.querySelector('[data-k="' + k + '"]').className;
    const out = [];
    out.push({ key: "ip", label: "PS5 address", state: c.ip_valid ? "ok" : "bad",
      hint: c.ip_valid ? c.ip : "No valid address set", fix: () => wizOpen(0) });
    const pOk = p && p.ok && (p.verified || cfg.allow_unverified);
    out.push({ key: "payload", label: "Payload pair verified", state: pOk ? "ok" : "bad",
      hint: !p ? "No m16cgpsp.lua + .bin pair found" : !p.ok ? p.problems.join("; ") :
        p.verified ? "SHA-256 matches the frozen release" : "SHA-256 differs from the frozen release. Pick another pair, or allow an unverified payload for development builds.",
      fix: () => { const el = $("payload-list"); el.scrollIntoView({ behavior: "smooth", block: "center" }); el.classList.remove("flash"); void el.offsetWidth; el.classList.add("flash"); } });
    const ps = c.payload || {};
    const running = ps.state === "running" || ps.state === "starting" ||
      (c.log && c.log.last_rx_age != null && c.log.last_rx_age < 30);
    if (running) {
      out.push({ key: "running", label: "Emulator already running on the console", state: "bad",
        hint: "Nothing needs launching. To start again: hold L1 + R1 + L2 + R2 to return to the ROM picker, press CIRCLE to exit, then re-arm the loader (Star Wars Racer Revenge > OPTIONS > HALL OF FAME) and press Launch.",
        fixLabel: "Check again", fix: () => wizOpen(3) });
    }
    const lst = cls("loader");
    out.push({ key: "loader", label: "Lua loader listening on TCP 9026", state: running ? "unknown" : lst === "ok" ? "ok" : lst === "bad" ? "bad" : "unknown",
      hint: running ? "Not listening while the emulator runs (expected)" : (c.loader ? c.loader.text : ""), fix: () => wizOpen(3) });
    const fst = cls("files");
    out.push({ key: "files", label: "BIOS and ROMs on the console", state: fst === "ok" ? "ok" : fst === "bad" ? "bad" : "unknown",
      hint: ck.querySelector('[data-k="files"]').title || (fst === "ok" ? "" : "Not verified yet: the console reports this when the payload starts"),
      fix: () => wizOpen(d.bios.ok ? 4 : 1) });
    return out;
  }

  async function startLaunch() {
    if (!state.data) return;
    const checks = launchChecks();
    if (checks.some((x) => x.state === "bad")) { openPrelaunch(checks); return; }
    await doLaunch();
  }

  async function doLaunch() {
    try {
      const r = await api("/api/launch", { with_roms: true });
      beginJob(r.job);
      toast(r.job.steps > 1 ? "Sending " + (r.job.steps - 1) + " game(s), then the payload…" : "Sending payload to " + state.data.config.ps5_ip + "…");
    } catch (e) { if (isBlocked(e)) openBlocked(e); else toast(e.message, true); }
  }

  function openBlocked(err) {
    const titles = { emulator_running: "Emulator is running - files cannot be sent",
      loader_down: "Loader is not listening - files cannot be sent", unreachable: "Console not reachable" };
    $("pl-title").textContent = titles[err.code] || "Cannot send right now";
    $("pl-rows").innerHTML = '<div class="pl-row bad"><span class="pl-ico ico-bad">✕</span><div><div class="pl-label">' +
      esc(err.code === "emulator_running" ? "Quit the emulator and re-arm the loader first" : "Arm the loader first") +
      '</div><div class="pl-hint">' + esc(err.message) + '</div></div>' +
      '<button class="btn btn-secondary btn-sm" id="pl-recheck">Check again</button></div>';
    $("pl-anyway").hidden = true;
    $("pl-cancel").textContent = "Close";
    $("prelaunch-modal").querySelector(".modal-head .muted").textContent = "Uploads are scripts for the Lua loader; it must be listening";
    $("prelaunch-modal").hidden = false;
    $("pl-recheck").addEventListener("click", async () => {
      $("pl-recheck").disabled = true;
      try {
        await api("/api/ping", {});
        const r = await api("/api/probe", {});
        await refresh(true);
        toast(r.probe.ok ? "Loader is listening now - send again." : "Still not listening: " + r.probe.error, !r.probe.ok);
        if (r.probe.ok) $("prelaunch-modal").hidden = true;
      } catch (e) { toast(e.message, true); }
      $("pl-recheck").disabled = false;
    });
  }

  function isBlocked(e) { return e && (e.code === "emulator_running" || e.code === "loader_down" || e.code === "unreachable"); }

  function openPrelaunch(checks) {
    $("pl-title").textContent = "Not ready to launch";
    $("pl-anyway").hidden = false;
    $("pl-cancel").textContent = "Cancel";
    $("prelaunch-modal").querySelector(".modal-head .muted").textContent = "Fix the red items, or launch anyway";
    const icons = { ok: "✓", bad: "✕", unknown: "?" }, colors = { ok: "ico-good", bad: "ico-bad", unknown: "ico-info" };
    $("pl-rows").innerHTML = checks.map((x, i) =>
      '<div class="pl-row ' + x.state + '"><span class="pl-ico ' + colors[x.state] + '">' + icons[x.state] + '</span>' +
      '<div><div class="pl-label">' + esc(x.label) + '</div><div class="pl-hint">' + esc(x.hint || "") + '</div></div>' +
      (x.state === "bad" ? '<button class="btn btn-secondary btn-sm" data-fix="' + i + '">' + esc(x.fixLabel || "Fix this") + '</button>' : '<span></span>') + '</div>').join("");
    $("pl-rows").querySelectorAll("button[data-fix]").forEach((b) => b.addEventListener("click", () => {
      $("prelaunch-modal").hidden = true;
      checks[Number(b.dataset.fix)].fix();
    }));
    $("prelaunch-modal").hidden = false;
  }

  async function startUpload(body) {
    try {
      const r = await api("/api/upload", body);
      beginJob(r.job);
      toast("Upload started: " + r.job.title);
    } catch (e) { if (isBlocked(e)) openBlocked(e); else toast(e.message, true); }
  }

  function beginJob(job) {
    state.jobRunning = true;
    state.jobNext = 0;
    $("log-job").innerHTML = "";
    if (job.kind === "upload" || job.kind === "upload_all" || job.kind === "launch_with_roms") openModal(job);
    else showTab("job");
    render();
    pollJob();
  }

  // ------------------------------------------------------------ setup wizard
  const wiz = { open: false, step: 0, probe: null, ping: null, busy: false, selected: null };
  const WIZ_STEPS = ["Console address", "GBA BIOS", "ROM folder", "Arm the loader", "Games to include", "Ready"];

  function wizOpen(step) {
    wiz.open = true;
    wiz.step = step || 0;
    wiz.probe = null; wiz.ping = null; wiz.selected = null;
    $("setup-modal").hidden = false;
    wizRender();
  }

  async function wizClose(markDone) {
    wiz.open = false;
    $("setup-modal").hidden = true;
    if (markDone) await saveConfig({ setup_done: true });
  }

  function wizResult(kind, html) {
    const icons = { good: "●", bad: "✕", warn: "▲", info: "…" };
    return '<div class="result ' + kind + '"><span class="ico ico-' + (kind === "info" ? "info" : kind) + '">' + icons[kind] + '</span><div>' + html + '</div></div>';
  }

  function wizRender() {
    if (!wiz.open || !state.data) return;
    const d = state.data, cfg = d.config, n = WIZ_STEPS.length, i = wiz.step;
    $("sw-kicker").textContent = "Setup · step " + (i + 1) + " of " + n;
    $("sw-title").textContent = WIZ_STEPS[i];
    $("sw-dots").innerHTML = WIZ_STEPS.map((_, k) => '<span class="' + (k < i ? "done" : k === i ? "cur" : "") + '"></span>').join("");
    $("sw-back").hidden = i === 0;
    $("sw-skip").hidden = i === n - 1;
    const next = $("sw-next");
    next.textContent = i === n - 1 ? "Finish" : "Next";
    next.disabled = wiz.busy;
    let html = "";
    if (i === 0) {
      html = '<p class="big">Enter your PlayStation 5\'s address on your local network.</p>' +
        '<div class="row"><input id="sw-ip" class="input" type="text" inputmode="decimal" placeholder="192.168.1.50" value="' + esc(cfg.ps5_ip) + '"><button id="sw-ping" class="btn btn-ghost">Test</button></div>' +
        (wiz.ping ? wizResult(wiz.ping.ok ? "good" : "bad", wiz.ping.ok ? "The console answered in " + wiz.ping.ms + " ms." : "No reply from " + esc(wiz.ping.ip) + ". Check the address and that the console is on and on the same network.") :
          '<p class="muted small">Find it on the console under Settings › Network › View Connection Status. The launcher pings it automatically from now on.</p>');
      next.disabled = wiz.busy || !d.console.ip_valid;
    } else if (i === 1) {
      const b = d.bios;
      html = '<p class="big">Point the launcher at your GBA BIOS file. It is checked immediately.</p>' +
        '<div class="row"><input id="sw-bios" class="input" type="text" placeholder="…\\gba_bios.bin" value="' + esc(cfg.bios_path) + '"><button id="sw-bios-browse" class="btn btn-ghost">Browse…</button></div>' +
        (b.path ? wizResult(b.ok ? "good" : "bad", b.ok ? "Verified: exactly 16,384 bytes · FNV-1a <code>" + esc(b.fnv1a) + "</code>. The console prints the same hash when it loads it." : esc(b.message)) :
          '<p class="muted small">Any region\'s BIOS is accepted: only the size is checked (exactly 16,384 bytes, a GBA requirement); LUAp0rt ships no BIOS and will not help you obtain one. Supply only firmware you are legally entitled to use.</p>');
      next.disabled = wiz.busy || !b.ok;
      html += '<p class="muted small">' + (b.ok ? "" : "You can also <a href=\"#\" id=\"sw-bios-skip\">skip this</a> if the BIOS is already on the console.") + '</p>';
    } else if (i === 2) {
      const r = d.roms;
      html = '<p class="big">Choose the folder on this PC that holds your <code>.gba</code> games.</p>' +
        '<div class="row"><input id="sw-rom" class="input" type="text" placeholder="…\\GBA ROMs" value="' + esc(cfg.rom_dir) + '"><button id="sw-rom-browse" class="btn btn-ghost">Browse…</button></div>' +
        (r && r.ok ? wizResult(r.count ? "good" : "warn", r.count + " game" + (r.count === 1 ? "" : "s") + " found" + (r.ignored ? " (" + r.ignored + " other file" + (r.ignored === 1 ? "" : "s") + " ignored)" : "") + (r.over_capacity ? ". The console picker lists at most " + r.capacity + "." : ".")) :
          (cfg.rom_dir ? wizResult("bad", esc(r ? r.message : "Folder not found")) : '<p class="muted small">Only files ending in .gba (any case) are used, exactly as the console does.</p>'));
      next.disabled = wiz.busy || !(r && r.ok && r.count > 0);
    } else if (i === 3) {
      const ps = d.console.payload || {};
      const running = ps.state === "running" || ps.state === "starting" ||
        (d.console.log && d.console.log.last_rx_age != null && d.console.log.last_rx_age < 30);
      html = '<p class="big">Start the Lua loader on the PlayStation 5, then check it from here.</p>' +
        (running ? wizResult("warn", "<b>The emulator is already running on the console</b> (" + esc(ps.detail || "payload logging") + "). The loader does not listen while it runs. Either keep playing, or exit it first: hold <b>L1 + R1 + L2 + R2</b> to return to the ROM picker, then press <b>CIRCLE</b>. Then arm the loader as below.") : "") +
        '<ol class="steps"><li>Run the <b>Luac0re listener</b>: launch <b>Star Wars Racer Revenge</b>.</li><li>Open <b>OPTIONS</b>.</li><li>Select <b>HALL OF FAME</b>. The loader is now listening on port 9026.</li></ol>' +
        '<div class="row"><button id="sw-probe" class="btn btn-secondary"' + (wiz.busy ? " disabled" : "") + '>' + (wiz.busy ? "Checking…" : "Check the loader") + '</button></div>' +
        (wiz.probe ? wizResult(wiz.probe.ok ? "good" : "bad", wiz.probe.ok ? "Loader is listening on TCP 9026 (" + wiz.probe.ms + " ms)." + (wiz.ping && !wiz.ping.ok ? " (Ping did not answer, but the loader port did.)" : "") :
          "Not listening: " + esc(wiz.probe.error) + ". Either the loader is not armed yet (make sure the console reached HALL OF FAME), or the emulator is already running (exit it: hold L1 + R1 + L2 + R2, then CIRCLE at the picker, then re-arm). Check again afterwards." +
          (wiz.ping ? "<br>Ping: " + (wiz.ping.ok ? "answered in " + wiz.ping.ms + " ms." : "no reply.") : "")) : "");
      next.disabled = wiz.busy || !(wiz.probe && wiz.probe.ok);
      if (wiz.probe && !wiz.probe.ok) html += '<p class="muted small"><a href="#" id="sw-probe-skip">Continue anyway</a></p>';
    } else if (i === 4) {
      const r = d.roms, b = d.bios;
      const total = r && r.ok ? r.count : 0;
      html = '<p class="big">Choose which games go to the console when you press Launch.</p>' +
        '<p>Nothing is uploaded here. When you press <b>Launch</b>, the checked games' + (b.ok ? " (and the BIOS)" : "") +
        ' are sent to <code>' + esc(d.facts.console_rom_dir) + '</code> first, then LUAp0rt starts. Games already on the console are skipped automatically, so nothing is sent twice.</p>' +
        '<div class="choices">' +
        '<div class="choice" id="sw-select-all"><span class="ico ico-good">●</span><div><div class="ch-title">Select all ' + total + ' game' + (total === 1 ? "" : "s") + ' for upload at launch</div><div class="ch-sub">Every game in the folder is checked. Recommended: you can still untick any game in the library later.</div></div></div>' +
        '<div class="choice" id="sw-select-none"><span class="ico ico-info">●</span><div><div class="ch-title">Pick games myself</div><div class="ch-sub">All games are unchecked. Tick the ones you want in the library; they are sent when you press Launch.</div></div></div></div>' +
        (wiz.selected ? wizResult("good", wiz.selected === "all" ? "All " + total + " games are checked for the next launch." : "No games are checked yet. Tick games in the library, then press Launch.") : "");
      next.disabled = wiz.busy || !wiz.selected;
    } else {
      html = '<p class="big">Setup is complete.</p>' +
        '<div class="launch-arrow">▶</div>' +
        '<p>Press <b>Launch</b> on the right: the checked games' + (d.bios.ok ? " and the BIOS" : "") + ' are sent to the console first (with a progress window), then the emulator starts. The console log at the bottom shows it booting, and the library then marks every game the console actually found.</p>' +
        '<p class="muted small">You can rerun this guide any time with the Setup button in the top bar.</p>';
    }
    $("sw-body").innerHTML = html;
    wizWire(i);
  }

  function wizWire(i) {
    const on = (id, ev, fn) => { const el = $(id); if (el) el.addEventListener(ev, fn); };
    if (i === 0) {
      on("sw-ip", "change", () => saveConfig({ ps5_ip: $("sw-ip").value }).then(wizRender));
      on("sw-ip", "keydown", (e) => { if (e.key === "Enter") $("sw-ip").blur(); });
      on("sw-ping", "click", async () => {
        const ip = $("sw-ip").value;          // read before any re-render
        wiz.busy = true; wizRender();
        try { await saveConfig({ ps5_ip: ip }); const r = await api("/api/ping", {}); wiz.ping = r.ping; state.data.console.ping = r.ping; }
        catch (e) { toast(e.message, true); }
        wiz.busy = false; wizRender();
      });
      setTimeout(() => { const el = $("sw-ip"); if (el) el.focus(); }, 50);
    } else if (i === 1) {
      on("sw-bios", "change", () => saveConfig({ bios_path: $("sw-bios").value }).then(wizRender));
      on("sw-bios-browse", "click", async () => {
        try { const r = await api("/api/browse", { kind: "bios", title: "Select your gba_bios.bin", initial: $("sw-bios").value }); if (r.path) await saveConfig({ bios_path: r.path }); }
        catch (e) { toast(e.message, true); }
        wizRender();
      });
      on("sw-bios-skip", "click", (e) => { e.preventDefault(); wiz.step = 2; wizRender(); });
    } else if (i === 2) {
      on("sw-rom", "change", () => saveConfig({ rom_dir: $("sw-rom").value }).then(wizRender));
      on("sw-rom-browse", "click", async () => {
        try { const r = await api("/api/browse", { kind: "dir", title: "Select your GBA ROM folder", initial: $("sw-rom").value }); if (r.path) await saveConfig({ rom_dir: r.path }); }
        catch (e) { toast(e.message, true); }
        wizRender();
      });
    } else if (i === 3) {
      on("sw-probe", "click", async () => {
        wiz.busy = true; wizRender();
        try { const p = await api("/api/ping", {}); wiz.ping = p.ping; state.data.console.ping = p.ping; } catch (e) { /* ping optional */ }
        try { const r = await api("/api/probe", {}); wiz.probe = r.probe; state.data.console.probe = r.probe; state.data.console.loader = r.loader; }
        catch (e) { wiz.probe = { ok: false, error: e.message }; }
        wiz.busy = false; wizRender(); render();
      });
      on("sw-probe-skip", "click", (e) => { e.preventDefault(); wiz.step = 4; wizRender(); });
    } else if (i === 4) {
      on("sw-select-all", "click", async () => { await saveConfig({ deselected: [] }); wiz.selected = "all"; wiz.step = 5; wizRender(); });
      on("sw-select-none", "click", async () => {
        const d = state.data;
        await saveConfig({ deselected: d && d.roms && d.roms.ok ? d.roms.roms.map((g) => g.name) : [] });
        wiz.selected = "none"; wiz.step = 5; wizRender();
      });
    }
  }

  function waitJob() {
    return new Promise((resolve) => {
      const tick = () => { if (!state.jobRunning) resolve(); else setTimeout(tick, 400); };
      setTimeout(tick, 600);
    });
  }

  // ------------------------------------------------------------ upload modal
  function fmtBytes(n) {
    if (n == null) return "";
    if (n < 1024) return n + " B";
    if (n < 1048576) return (n / 1024).toFixed(0) + " KB";
    return (n / 1048576).toFixed(1) + " MB";
  }

  function openModal(job) {
    $("upload-modal").hidden = false;
    $("um-close").hidden = true;
    $("um-cancel").hidden = false;
    renderModal(job);
  }

  function renderModal(j) {
    const modal = $("upload-modal");
    if (modal.hidden) return;
    const prog = (j.progress || []).filter((p) => p.kind !== "launch");
    const ip = state.data ? state.data.config.ps5_ip : "";
    const total = prog.reduce((a, p) => a + (p.size || 0), 0);
    const sent = prog.reduce((a, p) => a + Math.min(p.sent || 0, p.size || p.sent || 0), 0);
    const done = prog.filter((p) => p.state === "done").length;
    const failed = prog.filter((p) => p.state === "failed").length;
    const running = prog.find((p) => p.state === "running");
    $("um-title").textContent = j.running ? "Sending to " + ip : (j.cancelled ? "Cancelled" : (failed ? "Stopped with an error" : "All sent"));
    $("um-sub").textContent = prog.length + " file" + (prog.length === 1 ? "" : "s") + " · " + fmtBytes(total) + (j.running ? "" : " · " + done + " sent" + (failed ? ", " + failed + " failed" : ""));
    const pct = total ? Math.round(100 * sent / total) : (j.running ? 0 : 100);
    const of = $("um-overall-fill");
    of.style.width = pct + "%";
    of.className = "bar-fill " + (j.running ? "running" : (failed || j.cancelled ? "failed" : "done"));
    $("um-overall-left").textContent = (j.running ? "sending " + (j.step || 1) + " of " + prog.length : "finished") + (running && running.rate ? " · " + running.rate.toFixed(1) + " MB/s" : "");
    $("um-overall-right").textContent = fmtBytes(sent) + " / " + fmtBytes(total) + " · " + pct + "%";
    const icons = { pending: "○", running: "●", done: "✓", failed: "✕", cancelled: "–", skipped: "–" };
    const colors = { pending: "ico-info", running: "ico-info", done: "ico-good", failed: "ico-bad", cancelled: "ico-warn", skipped: "ico-warn" };
    $("um-rows").innerHTML = prog.map((p) => {
      const ppct = p.size ? Math.min(100, Math.round(100 * (p.sent || 0) / p.size)) : (p.state === "done" ? 100 : 0);
      const meta = p.state === "running" ? fmtBytes(p.sent || 0) + " / " + fmtBytes(p.size) + (p.rate ? " · " + p.rate.toFixed(1) + " MB/s" : "") + " · " + ppct + "%" :
        p.state === "done" ? fmtBytes(p.size) + " · sent" :
        p.state === "failed" ? "failed" : p.state === "pending" ? fmtBytes(p.size) + " · waiting" : p.state;
      return '<div class="um-row ' + p.state + '">' +
        '<span class="um-ico ' + (colors[p.state] || "") + '">' + (icons[p.state] || "○") + '</span>' +
        '<span class="um-name" title="' + esc(p.remote) + '">' + esc(p.name) + '</span>' +
        '<span class="um-meta">' + esc(meta) + '</span>' +
        '<div class="bar"><div class="bar-fill ' + (p.state === "running" ? "running" : p.state === "done" ? "done" : p.state === "failed" ? "failed" : "") + '" style="width:' + ppct + '%"></div></div>' +
        '<div class="um-note">' + esc(p.note || "") + '</div></div>';
    }).join("");
    if (!j.running) {
      $("um-cancel").hidden = true;
      $("um-close").hidden = false;
    }
  }

  let jobTimer = null;
  async function pollJob() {
    clearTimeout(jobTimer);
    try {
      const r = await api("/api/job?since=" + state.jobNext);
      const j = r.job;
      if (!j) { state.jobRunning = false; render(); return; }
      appendLines($("log-job"), j.lines.map((s) => ({ s })), true);
      state.jobNext = j.next;
      renderModal(j);
      if (j.kind === "launch_with_roms" && !$("upload-modal").hidden) {
        const last = (j.progress || [])[j.progress.length - 1];
        if (last && last.kind === "launch" && last.state !== "pending") {
          // every game is on the console; the payload is now being sent
          $("upload-modal").hidden = true;
          showTab("console");
          toast("Games sent. Launching LUAp0rt GBA…");
        }
      }
      const badge = $("job-badge");
      badge.hidden = false;
      const prog = j.steps > 1 ? " " + (j.running ? j.step : j.steps_done) + "/" + j.steps : "";
      badge.textContent = (j.running ? "running" : (j.rc === 0 && !j.cancelled ? "done" : "failed")) + prog;
      badge.className = "count" + (j.running ? " live" : "");
      if (j.running) {
        state.jobRunning = true;
        jobTimer = setTimeout(pollJob, 500);
      } else if (state.jobRunning) {
        state.jobRunning = false;
        if (j.cancelled) toast("Job cancelled");
        else if (j.rc === 0) toast(j.kind === "launch" || j.kind === "launch_with_roms" ? "Payload sent. Watch the console log." :
          (j.steps > 1 ? "All " + j.steps + " games sent." : "Upload complete."));
        else if (j.steps > 1) toast("Batch stopped after " + j.steps_done + " of " + j.steps + " games. See tool output.", true);
        else toast("Job failed (exit " + j.rc + "). See tool output.", true);
        await refresh(true);
      }
    } catch (e) {
      jobTimer = setTimeout(pollJob, 1500);
    }
  }

  // ------------------------------------------------------------ logs
  function classify(s) {
    const u = s.toUpperCase();
    if (/\bFAIL|ERROR|EXCEPTION|TRACEBACK|REFUS|CANNOT|NO ACK|NO LIVE RECEIVER/.test(u)) return "bad";
    if (/WARN/.test(u)) return "warn";
    if (/\bPASS\b|\bOK\b|SENT\.|COMPLETE|FINISHED|VERDICT/.test(u)) return "good";
    if (/^===/.test(s)) return "head";
    if (/^\$ /.test(s)) return "cmd";
    return "";
  }

  function appendLines(pre, lines, isJob) {
    const filter = $("log-filter").value.trim().toLowerCase();
    const follow = $("log-follow").checked;
    const atBottom = pre.scrollHeight - pre.scrollTop - pre.clientHeight < 40;
    const frag = document.createDocumentFragment();
    for (const l of lines) {
      const span = document.createElement("span");
      span.className = "ln " + classify(l.s);
      span.dataset.raw = l.s.toLowerCase();
      if (!isJob && l.t) {
        const ts = document.createElement("span");
        ts.className = "ts";
        ts.textContent = new Date(l.t * 1000).toLocaleTimeString([], { hour12: false });
        span.appendChild(ts);
      }
      span.appendChild(document.createTextNode(l.s + "\n"));
      if (filter && !span.dataset.raw.includes(filter)) span.hidden = true;
      frag.appendChild(span);
    }
    pre.appendChild(frag);
    while (pre.childElementCount > 6000) pre.removeChild(pre.firstChild);
    if (follow && (atBottom || lines.length)) pre.scrollTop = pre.scrollHeight;
  }

  function applyFilter() {
    const filter = $("log-filter").value.trim().toLowerCase();
    document.querySelectorAll(".log .ln").forEach((el) => {
      el.hidden = !!filter && !el.dataset.raw.includes(filter);
    });
  }

  async function pollLog() {
    if (state.stopped) return;
    try {
      const r = await api("/api/log?since=" + state.logNext);
      noteSuccess();
      if (r.lines.length) {
        appendLines($("log-console"), r.lines, false);
        state.logNext = r.next;
      } else if (r.next < state.logNext) {
        state.logNext = r.next; // buffer was cleared server-side
      }
      const cnt = $("log-count");
      cnt.textContent = r.status.total;
      $("logs-summary").textContent = "· " + r.status.total + " line" + (r.status.total === 1 ? "" : "s") +
        (r.status.last_rx_age != null && r.status.last_rx_age < 5 ? " · live" : "");
      cnt.className = "count" + (r.status.last_rx_age != null && r.status.last_rx_age < 5 ? " live" : "");
      if (state.data) { state.data.console.log = r.status; }
    } catch (e) { noteFailure(); }
    setTimeout(pollLog, 600);
  }

  function setLogsCollapsed(collapsed) {
    $("logs-card").classList.toggle("collapsed", collapsed);
    $("logs-toggle").textContent = collapsed ? "▸" : "▾";
    $("logs-toggle").setAttribute("aria-expanded", collapsed ? "false" : "true");
  }

  function showTab(name) {
    setLogsCollapsed(false);
    state.activeTab = name;
    document.querySelectorAll(".tab").forEach((t) => t.classList.toggle("active", t.dataset.tab === name));
    $("log-console").hidden = name !== "console";
    $("log-job").hidden = name !== "job";
    $("btn-log-save").hidden = name !== "console";
  }

  // ------------------------------------------------------------ wiring
  function wire() {
    $("ip").addEventListener("change", () => saveConfig({ ps5_ip: $("ip").value }));
    $("ip").addEventListener("keydown", (e) => { if (e.key === "Enter") $("ip").blur(); });
    $("rom-dir").addEventListener("change", () => {
      const cur = state.data ? state.data.config.rom_dir : "";
      const next = $("rom-dir").value.trim();
      if (cur && next !== cur && !window.confirm("Change the ROMs folder?\n\nThe library will show the games in the new folder instead; the current list is replaced (nothing is deleted). Continue?")) {
        $("rom-dir").value = cur; return;
      }
      saveConfig({ rom_dir: next });
    });
    $("bios-path").addEventListener("change", () => saveConfig({ bios_path: $("bios-path").value }));
    $("allow-unverified").addEventListener("change", () => saveConfig({ allow_unverified: $("allow-unverified").checked }));
    $("rom-filter").addEventListener("input", () => render());
    $("btn-rescan").addEventListener("click", () => refresh(true));
    $("btn-check-all").addEventListener("click", () => saveConfig({ deselected: [] }));
    $("btn-uncheck-all").addEventListener("click", () => { const d = state.data; if (d && d.roms && d.roms.ok) saveConfig({ deselected: d.roms.roms.map((g) => g.name) }); });
    $("sel-all").addEventListener("change", () => { const d = state.data; if (d && d.roms && d.roms.ok) saveConfig({ deselected: $("sel-all").checked ? [] : d.roms.roms.map((g) => g.name) }); });
    $("btn-quit").addEventListener("click", async () => {
      if (!window.confirm("Stop the LUAp0rt Launcher server? The console keeps running; only this dashboard closes.")) return;
      try { await api("/api/quit", {}); } catch (e) { /* server is going away */ }
      serverGone("You quit the launcher.");
    });

    $("btn-rom-browse").addEventListener("click", async () => {
      const cur = state.data ? state.data.config.rom_dir : "";
      if (cur && !window.confirm("Change the ROMs folder?\n\nThe library will show the games in the new folder instead; the current list is replaced (nothing is deleted). Continue?")) return;
      try {
        const r = await api("/api/browse", { kind: "dir", title: "Select your GBA ROM folder", initial: $("rom-dir").value });
        if (r.path) saveConfig({ rom_dir: r.path });
      } catch (e) { toast(e.message, true); }
    });

    // ---- drag & drop .gba files onto the library: copied into the ROM folder
    const card = document.querySelector(".card-library");
    let dragDepth = 0;
    card.addEventListener("dragenter", (e) => { e.preventDefault(); dragDepth++; card.classList.add("dragover"); $("drop-hint").hidden = false; });
    card.addEventListener("dragover", (e) => { e.preventDefault(); e.dataTransfer.dropEffect = "copy"; });
    card.addEventListener("dragleave", () => { dragDepth = Math.max(0, dragDepth - 1); if (!dragDepth) { card.classList.remove("dragover"); $("drop-hint").hidden = true; } });
    card.addEventListener("drop", async (e) => {
      e.preventDefault();
      dragDepth = 0; card.classList.remove("dragover"); $("drop-hint").hidden = true;
      const files = Array.from(e.dataTransfer.files || []);
      if (!files.length) return;
      if (!state.data || !state.data.config.rom_dir) { toast("Choose a ROM folder first", true); return; }
      let added = 0, skipped = [];
      for (const f of files) {
        if (!f.name.toLowerCase().endsWith(".gba")) { skipped.push(f.name + " (not .gba)"); continue; }
        try {
          toast("Copying " + f.name + "…");
          const r = await fetch("/api/roms/add?name=" + encodeURIComponent(f.name), { method: "POST", body: f });
          const jr = await r.json();
          if (jr.error && jr.code === "exists") {
            if (window.confirm(f.name + " is already in the folder. Replace it?")) {
              const r2 = await fetch("/api/roms/add?name=" + encodeURIComponent(f.name) + "&overwrite=1", { method: "POST", body: f });
              const j2 = await r2.json();
              if (j2.error) { skipped.push(f.name + " (" + j2.error + ")"); continue; }
            } else { skipped.push(f.name + " (kept existing)"); continue; }
          } else if (jr.error) { skipped.push(f.name + " (" + jr.error + ")"); continue; }
          added++;
        } catch (err) { skipped.push(f.name + " (" + err.message + ")"); }
      }
      await refresh(true);
      toast((added ? added + " game" + (added === 1 ? "" : "s") + " added to the folder" : "Nothing added") + (skipped.length ? " · skipped: " + skipped.join(", ") : ""), skipped.length > 0 && !added);
    });
    $("btn-rom-open").addEventListener("click", async () => {
      try { await api("/api/open", { path: $("rom-dir").value }); } catch (e) { toast(e.message, true); }
    });
    $("btn-bios-browse").addEventListener("click", async () => {
      try {
        const r = await api("/api/browse", { kind: "bios", title: "Select your gba_bios.bin", initial: $("bios-path").value || $("rom-dir").value });
        if (r.path) saveConfig({ bios_path: r.path });
      } catch (e) { toast(e.message, true); }
    });

    $("btn-ping").addEventListener("click", async () => {
      if (state.pingBusy) return;
      state.pingBusy = true;
      $("btn-ping").disabled = true;
      try {
        await saveConfig({ ps5_ip: $("ip").value });
        const r = await api("/api/ping", {});
        state.data.console.ping = r.ping;
        render();
        toast(r.ping.ok ? "Console answered in " + r.ping.ms + " ms" : "No ping reply from " + r.ping.ip, !r.ping.ok);
      } catch (e) { toast(e.message, true); }
      state.pingBusy = false;
      $("btn-ping").disabled = false;
    });

    $("probe-loader").addEventListener("change", () => saveConfig({ probe_loader: $("probe-loader").checked }));
    $("btn-probe").addEventListener("click", async () => {
      $("btn-probe").disabled = true;
      try {
        const r = await api("/api/probe", {});
        state.data.console.probe = r.probe;
        state.data.console.loader = r.loader;
        render();
        toast(r.probe.ok ? "Loader is listening on TCP 9026 (" + r.probe.ms + " ms)" : "Loader not listening: " + r.probe.error, !r.probe.ok);
      } catch (e) { toast(e.message, true); }
      $("btn-probe").disabled = false;
    });
    $("um-cancel").addEventListener("click", async () => {
      try { await api("/api/job/cancel", {}); } catch (e) { toast(e.message, true); }
    });
    $("um-close").addEventListener("click", () => { $("upload-modal").hidden = true; });
    $("um-output").addEventListener("click", () => { $("upload-modal").hidden = true; showTab("job"); $("log-job").scrollIntoView({ behavior: "smooth", block: "end" }); });
    $("upload-modal").addEventListener("click", (e) => { if (e.target === $("upload-modal") && !$("um-close").hidden) $("upload-modal").hidden = true; });
    $("btn-setup").addEventListener("click", () => wizOpen(0));
    $("sw-back").addEventListener("click", () => { if (wiz.step > 0) { wiz.step--; wizRender(); } });
    $("sw-next").addEventListener("click", () => {
      if (wiz.step >= WIZ_STEPS.length - 1) { wizClose(true); return; }
      wiz.step++; wizRender();
    });
    $("sw-skip").addEventListener("click", () => {
      if (window.confirm("Skip the guided setup? You can run it again from the Setup button.")) wizClose(true);
    });
    $("btn-launch").addEventListener("click", startLaunch);
    $("pl-cancel").addEventListener("click", () => { $("prelaunch-modal").hidden = true; });
    $("pl-anyway").addEventListener("click", () => { $("prelaunch-modal").hidden = true; doLaunch(); });
    $("btn-cancel").addEventListener("click", async () => {
      try { await api("/api/job/cancel", {}); } catch (e) { toast(e.message, true); }
    });

    document.querySelectorAll(".tab").forEach((t) => t.addEventListener("click", () => showTab(t.dataset.tab)));
    $("logs-head").addEventListener("click", (e) => {
      if (e.target.closest(".tabs") || e.target.closest(".card-tools")) return;   // tabs and tools keep their own actions
      setLogsCollapsed(!$("logs-card").classList.contains("collapsed"));
    });
    $("log-filter").addEventListener("input", applyFilter);
    $("btn-log-clear").addEventListener("click", async () => {
      if (state.activeTab === "job") { $("log-job").innerHTML = ""; return; }
      try { await api("/api/log/clear", {}); $("log-console").innerHTML = ""; state.logNext = 0; } catch (e) { toast(e.message, true); }
    });
  }

  wire();
  refresh(true).then(() => {
    if (state.data && !state.data.config.setup_done) wizOpen(0);
  });
  pollLog();
  setInterval(() => refresh(false), 6000);
  // resume tracking a job that was already running when the page opened
  api("/api/job?since=0").then((r) => {
    if (r.job && r.job.running) {
      state.jobRunning = true;
      if (r.job.kind === "upload" || r.job.kind === "upload_all" || r.job.kind === "launch_with_roms") openModal(r.job);
      pollJob();
    }
  }).catch(() => {});
})();
