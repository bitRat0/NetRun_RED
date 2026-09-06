/* Dependency-free Chromium/Edge CDP acceptance. Runtime tool remains file:// only.
 * NODE >=22; BROWSER_EXE may point to a Chromium-family executable.
 * Artifacts go in a fresh OS temporary directory, which is reported and retained. */
const fs = require("node:fs"),
  os = require("node:os"),
  path = require("node:path");
const { spawn } = require("node:child_process");
const { pathToFileURL } = require("node:url");
const assert = require("node:assert/strict");
const M = require("../model.js");
const pause = (ms) => new Promise((r) => setTimeout(r, ms));
async function run() {
  const executable =
    process.env.BROWSER_EXE ||
    "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";
  if (!fs.existsSync(executable))
    throw Error("Set BROWSER_EXE to an installed Chromium/Edge executable.");
  const artifacts = fs.mkdtempSync(
    path.join(os.tmpdir(), "netrun-builder-check-"),
  );
  const profile = path.join(artifacts, "profile"),
    downloads = path.join(artifacts, "downloads");
  fs.mkdirSync(downloads);
  const child = spawn(
    executable,
    [
      "--headless=new",
      "--disable-gpu",
      "--no-first-run",
      "--no-default-browser-check",
      "--disable-extensions",
      "--disable-background-networking",
      "--remote-debugging-port=0",
      `--user-data-dir=${profile}`,
      "about:blank",
    ],
    { windowsHide: true, stdio: ["ignore", "ignore", "pipe"] },
  );
  let log = "";
  child.stderr.on("data", (d) => {
    log += d;
  });
  child.on("error", (e) => {
    log += e.message;
  });
  let socket;
  const pending = new Map();
  let id = 0,
    session;
  const consoleErrors = [],
    requests = [];
  let acceptDialogs = true;
  function send(method, params = {}, sid = session) {
    return new Promise((resolve, reject) => {
      const n = ++id,
        timer = setTimeout(() => {
          pending.delete(n);
          reject(Error(`CDP timeout ${method}`));
        }, 15000);
      pending.set(n, {
        resolve: (r) => {
          clearTimeout(timer);
          resolve(r);
        },
        reject: (e) => {
          clearTimeout(timer);
          reject(e);
        },
      });
      socket.send(
        JSON.stringify({
          id: n,
          method,
          params,
          ...(sid ? { sessionId: sid } : {}),
        }),
      );
    });
  }
  try {
    const portFile = path.join(profile, "DevToolsActivePort");
    for (let n = 0; !fs.existsSync(portFile) && n < 150; n++) await pause(100);
    if (!fs.existsSync(portFile))
      throw Error("Browser failed to launch: " + log.slice(-2000));
    const [port, endpoint] = fs
      .readFileSync(portFile, "utf8")
      .trim()
      .split("\n");
    socket = new WebSocket(`ws://127.0.0.1:${port}${endpoint}`);
    await new Promise((resolve, reject) => {
      socket.addEventListener("open", resolve, { once: true });
      socket.addEventListener("error", reject, { once: true });
    });
    socket.addEventListener("message", (event) => {
      const m = JSON.parse(event.data);
      if (m.id) {
        const p = pending.get(m.id);
        if (p) {
          pending.delete(m.id);
          m.error
            ? p.reject(Error(JSON.stringify(m.error)))
            : p.resolve(m.result);
        }
        return;
      }
      if (m.method === "Runtime.exceptionThrown")
        consoleErrors.push(m.params.exceptionDetails);
      if (m.method === "Log.entryAdded" && m.params.entry.level === "error")
        consoleErrors.push(m.params.entry);
      if (m.method === "Network.requestWillBeSent")
        requests.push(m.params.request.url);
      if (m.method === "Page.javascriptDialogOpening")
        send("Page.handleJavaScriptDialog", { accept: acceptDialogs }).catch(
          () => {},
        );
    });
    console.log("Browser:", await send("Browser.getVersion", {}, null));
    const target = await send(
      "Target.createTarget",
      { url: "about:blank" },
      null,
    );
    session = (
      await send(
        "Target.attachToTarget",
        { targetId: target.targetId, flatten: true },
        null,
      )
    ).sessionId;
    for (const domain of ["Page", "Runtime", "Log", "Network", "DOM"])
      await send(domain + ".enable");
    await send("Emulation.setDeviceMetricsOverride", {
      width: 1440,
      height: 1100,
      deviceScaleFactor: 1,
      mobile: false,
    });
    await send("Network.emulateNetworkConditions", {
      offline: true,
      latency: 0,
      downloadThroughput: 0,
      uploadThroughput: 0,
    });
    await send(
      "Browser.setDownloadBehavior",
      { behavior: "allow", downloadPath: downloads },
      null,
    );
    const url = pathToFileURL(path.resolve(__dirname, "../index.html")).href;
    await send("Page.navigate", { url });
    async function evaluate(expression) {
      const r = await send("Runtime.evaluate", {
        expression,
        awaitPromise: true,
        returnByValue: true,
      });
      if (r.exceptionDetails)
        throw Error(
          r.exceptionDetails.exception?.description || r.exceptionDetails.text,
        );
      return r.result.value;
    }
    for (let i = 0; i < 100; i++) {
      if (
        await evaluate(
          'typeof NRModel!=="undefined" && !!document.querySelector("#navigation button")',
        )
      )
        break;
      await pause(50);
    }
    assert.ok(
      await evaluate('!!document.querySelector("#navigation button")'),
      "App did not initialize",
    );
    const click = async (text) => {
      const point = await evaluate(
        `(()=>{const b=[...document.querySelectorAll('button,summary')].find(b=>b.textContent===${JSON.stringify(text)});if(!b)throw Error('Missing button '+${JSON.stringify(text)});if(b.disabled)throw Error('Disabled button');b.scrollIntoView({block:'center'});const r=b.getBoundingClientRect();if(!r.width||!r.height)throw Error('Hidden control '+${JSON.stringify(text)});return {x:r.x+r.width/2,y:r.y+r.height/2};})()`,
      );
      await send("Input.dispatchMouseEvent", {
        type: "mousePressed",
        button: "left",
        clickCount: 1,
        ...point,
      });
      await send("Input.dispatchMouseEvent", {
        type: "mouseReleased",
        button: "left",
        clickCount: 1,
        ...point,
      });
    };
    const field = async (p, value) =>
      evaluate(
        `(()=>{const e=[...document.querySelectorAll('[data-path]')].find(e=>e.dataset.path===${JSON.stringify(p)});if(!e)throw Error('Missing field '+${JSON.stringify(p)});e.value=${JSON.stringify(String(value))};e.dispatchEvent(new Event('input',{bubbles:true}));e.dispatchEvent(new Event('change',{bubbles:true}));})()`,
      );
    const json = () =>
      evaluate('JSON.parse(document.getElementById("json").value)');
    const ready = async () =>
      assert.equal(
        await evaluate('document.getElementById("download").disabled'),
        false,
        await evaluate('document.getElementById("issues").innerText'),
      );
    const reload = async () => {
      await send("Page.reload", { ignoreCache: true });
      for (let i = 0; i < 100; i++) {
        if (await evaluate("typeof NRModel!== 'undefined' && !!document.querySelector('#navigation button')")) break;
        await pause(50);
      }
      await ready();
    };
    const checkpoint = (text) => console.log("PASS:", text);
    const importFile = async (file) => {
      const { root } = await send("DOM.getDocument");
      const { nodeId } = await send("DOM.querySelector", {
        nodeId: root.nodeId,
        selector: "#file",
      });
      await send("DOM.setFileInputFiles", { nodeId, files: [file] });
      await pause(150);
    };
    const download = async () => {
      const doc = await json(),
        kind = M.identify(doc),
        filename = M.destination(doc, kind).split("/").pop();
      await click("Download JSON");
      const dest = path.join(downloads, filename);
      for (let n = 0; n < 100 && !fs.existsSync(dest); n++) await pause(50);
      assert.ok(fs.existsSync(dest), "Download missing " + filename);
      return { dest, text: fs.readFileSync(dest, "utf8"), doc, kind };
    };
    await ready();
    await field("$.id", "browser_run");
    await field("$.name", "BROWSER RUN");
    await click("Add floor");
    await click("Add floor");
    await evaluate(
      `(()=>{const input=document.querySelector('[aria-label="Connection map"]');input.value="0: 1, 2";input.dispatchEvent(new Event("input", { bubbles: true }));})()`,
    );
    await ready();
    assert.deepEqual((await json()).floors[0].next, [1, 2]);
    await pause(300);
    const savedBeforeReload = await evaluate(`JSON.parse(localStorage.getItem("netrun-red-scenario-builder-workspace"))`);
    assert.equal(savedBeforeReload.workspaceVersion, 1);
    assert.equal(savedBeforeReload.docs.scenario.name, "BROWSER RUN");
    assert.equal(savedBeforeReload.docs.scenario.floors.length, 3);
    await reload();
    assert.equal((await json()).name, "BROWSER RUN");
    assert.deepEqual((await json()).floors[0].next, [1, 2]);
    assert.equal(await evaluate(`document.getElementById("notice").textContent`), "Restored local workspace");
    checkpoint("file:// local workspace autosave and reload restore");
    checkpoint("file:// offline: scenario from scratch, floors, branch graph");
    await click("Floor 0");
    await field("$.floors[0].id", 1);
    assert.equal(
      await evaluate('document.getElementById("download").disabled'),
      true,
    );
    await field("$.floors[0].id", 0);
    await ready();
    checkpoint("live duplicate-ID error blocks export and clears after repair");
    await click("Load starter set");
    await ready();
    await click("Floor 1");
    assert.equal(
      await evaluate('document.querySelectorAll(".placement canvas").length'),
      2,
    );
    checkpoint(
      "scenario demon, custom ICE and enemy references render loaded thumbnails",
    );
    await click("Black ICE");
    await ready();
    const spriteImages = [];
    for (const visual of M.D.visuals) {
      await field("$.black_ice[0].visual", visual);
      spriteImages.push(
        await evaluate('document.querySelector(".preview canvas").toDataURL()'),
      );
      assert.equal((await json()).black_ice[0].visual, visual);
    }
    assert.equal(new Set(spriteImages).size, 12);
    checkpoint(
      "all 12 visual selectors immediately draw distinct firmware sprites",
    );
    for (const animation of M.D.animations) {
      await field("$.black_ice[0].animation", animation);
      await click("ATTACK");
      await pause(100);
      await click("REPLAY");
      await click("REVEAL");
      await pause(100);
      await click("IDLE");
      assert.equal((await json()).black_ice[0].animation, animation);
    }
    // Also render every frame through real Canvas, not a mocked context.
    assert.ok(
      await evaluate(
        `(()=>{const c=document.createElement('canvas');c.width=480;c.height=270;for(const v of NRData.visuals)for(const a of NRData.animations)for(let f=0;f<8;f++)NRPreview.draw(c,v,a,'attack',f*45);for(const v of NRData.visuals)for(let f=0;f<6;f++)NRPreview.draw(c,v,'pulse','reveal',f*70);return true;})()`,
      ),
    );
    checkpoint(
      "all 4 attack styles, replay/reveal, 384 ICE attack and 72 reveal frames offline",
    );
    await evaluate(
      'document.querySelector(".preview").scrollIntoView({block:"center"})',
    );
    const screenshot = await send("Page.captureScreenshot", {
      format: "png",
      captureBeyondViewport: false,
    });
    fs.writeFileSync(
      path.join(artifacts, "black-ice-editor.png"),
      Buffer.from(screenshot.data, "base64"),
    );
    const gallery = await evaluate(
      `(()=>{const c=document.createElement('canvas');c.width=640;c.height=450;const x=c.getContext('2d');x.fillStyle='#100d14';x.fillRect(0,0,640,450);NRData.visuals.forEach((v,i)=>{const t=document.createElement('canvas');t.width=t.height=112;NRPreview.thumbnail(t,v);x.drawImage(t,i%4*160+24,Math.floor(i/4)*150+8);x.fillStyle='#eee';x.font='14px monospace';x.fillText(v,i%4*160+30,Math.floor(i/4)*150+140);});return c.toDataURL().split(',')[1];})()`,
    );
    fs.writeFileSync(
      path.join(artifacts, "visual-gallery.png"),
      Buffer.from(gallery, "base64"),
    );
    await click("Demons");
    await field("$.demons[0].visual", "sentinel");
    await field("$.demons[0].animation", "slash");
    assert.equal((await json()).demons[0].visual, "sentinel");
    assert.equal((await json()).demons[0].animation, "slash");
    await click("ATTACK");
    await pause(100);
    await click("REPLAY");
    await click("IDLE");
    await ready();
    await click("Enemy Netrunners");
    for (const b of M.behaviors) {
      await field("$.enemies[0].behavior", b);
      await ready();
      assert.equal((await json()).enemies[0].behavior, b);
    }
    await field("$.enemies[0].programs[0]", "Vrizzbolt");
    assert.equal((await json()).enemies[0].programs[0], "Vrizzbolt");
    await click("ATTACK");
    await pause(100);
    await click("IDLE");
    checkpoint(
      "Demon visual/animation selectors, attack replay, deck form, all 3 enemy behaviors",
    );
    const exported = [];
    for (const label of [
      "Black ICE",
      "Demons",
      "Enemy Netrunners",
      "Scenario / Architecture",
    ]) {
      await click(label);
      await ready();
      const out = await download();
      assert.equal(out.text.includes("workspaceVersion"), false);
      assert.deepEqual(JSON.parse(out.text), out.doc);
      exported.push(out);
    }
    for (const out of exported) {
      await importFile(out.dest);
      assert.deepEqual(await json(), out.doc);
      assert.equal(
        await evaluate('document.getElementById("json").value'),
        out.text,
      );
      await ready();
    }
    checkpoint(
      "four real Blob downloads, file imports and deterministic roundtrip; no UI/preview metadata",
    );
    await click("Scenario / Architecture");
    await click("Advanced / LOCAL & PORTABLE definitions");
    await click("Black ICE / local");
    await click("Enable local Black ICE");
    await click("Create Black ICE");
    await field("$.black_ice[0].id", "signal_ice");
    assert.equal(
      await evaluate('document.getElementById("download").disabled'),
      true,
    );
    await field("$.black_ice[0].id", "local_signal");
    await ready();
    assert.ok(
      await evaluate(
        'document.querySelector("details[data-detail=local_black_ice]").open',
      ),
      "Local editor collapsed during edit",
    );
    await click("Floor 1");
    await field("$.floors[1].ice[0]", "local_signal");
    await ready();
    assert.equal((await json()).floors[1].ice[0], "local_signal");
    checkpoint(
      "explicit portable definition form, local/global conflict and repaired local placement",
    );
    await pause(300);
    await reload();
    await click("Black ICE");
    assert.ok((await json()).black_ice.length >= 1, "Black ICE catalog did not restore");
    await click("Demons");
    assert.equal((await json()).demons[0].visual, "sentinel");
    assert.equal((await json()).demons[0].animation, "slash");
    await click("Enemy Netrunners");
    assert.equal((await json()).enemies[0].programs[0], "Vrizzbolt");
    await click("Scenario / Architecture");
    checkpoint("catalog workspaces and Demon presentation restore after reload");
    await click("Demons / local");
    await click("Enable local Demons");
    await click("Create Demons");
    await field("$.demons[0].id", "local_relay");
    await click("Scenario / Architecture");
    await field("$.demon", "local_relay");
    await click("Enemy Netrunners / local");
    await click("Enable local Enemy Netrunners");
    await click("Create Enemy Netrunners");
    await field("$.enemies[0].id", "local_runner");
    await field("$.enemies[0].behavior", "defensive");
    await field("$.floors[1].enemy", "local_runner");
    await ready();
    assert.equal((await json()).demons[0].id, "local_relay");
    assert.equal((await json()).enemies[0].behavior, "defensive");
    checkpoint(
      "all three LOCAL editors, demon root and independent enemy placement",
    );
    await click("Black ICE");
    await click("Duplicate definition");
    assert.equal((await json()).black_ice.length, 2);
    await click("Delete definition");
    assert.equal((await json()).black_ice.length, 1);
    checkpoint("catalog duplicate/delete controls preserve valid identities");
    const prior = await json();
    await evaluate(
      `(()=>{const t=document.getElementById('json');const d=JSON.parse(t.value);d.black_ice[0].preview={frame:2};t.value=JSON.stringify(d);t.dispatchEvent(new Event('input'));})()`,
    );
    assert.equal(
      await evaluate('document.getElementById("download").disabled'),
      true,
    );
    await click("Apply JSON edits");
    assert.equal(
      await evaluate('document.getElementById("download").disabled'),
      true,
    );
    assert.deepEqual((await json()).black_ice[0].preview, { frame: 2 });
    await evaluate(
      `(()=>{const t=document.getElementById('json');t.value=${JSON.stringify(JSON.stringify(prior))};t.dispatchEvent(new Event('input'));})()`,
    );
    await click("Apply JSON edits");
    await ready();
    checkpoint(
      "unknown import fields retained and block export; explicit JSON repair",
    );
    for (const visual of [
      99,
      null,
      "toString",
      "__proto__",
      "missing",
      { toString: "not callable" },
    ]) {
      const bad = M.clone(prior);
      bad.black_ice[0].visual = visual;
      await evaluate(
        `(()=>{const t=document.getElementById('json');t.value=${JSON.stringify(JSON.stringify(bad))};t.dispatchEvent(new Event('input'));})()`,
      );
      await click("Apply JSON edits");
      assert.equal(
        await evaluate('document.getElementById("download").disabled'),
        true,
      );
      assert.deepEqual((await json()).black_ice[0].visual, visual);
    }
    await evaluate(
      `(()=>{const t=document.getElementById('json');t.value=${JSON.stringify(JSON.stringify(prior))};t.dispatchEvent(new Event('input'));})()`,
    );
    await click("Apply JSON edits");
    await ready();
    checkpoint(
      "malformed visual types / prototype-like IDs remain editable without rendering crashes",
    );
    await send("Emulation.setDeviceMetricsOverride", {
      width: 960,
      height: 900,
      deviceScaleFactor: 1,
      mobile: false,
    });
    assert.ok(
      await evaluate("document.documentElement.scrollWidth <= innerWidth"),
      "Laptop layout overflow",
    );
    const shot = await send("Page.captureScreenshot", { format: "png" });
    fs.writeFileSync(
      path.join(artifacts, "laptop-editor.png"),
      Buffer.from(shot.data, "base64"),
    );
    await click("Scenario / Architecture");
    await click("New Scenario");
    await pause(300);
    assert.equal((await json()).id, "new_run");
    await click("Black ICE");
    assert.ok((await json()).black_ice.length > 0, "New Scenario must preserve catalogs");
    await click("Clear local workspace");
    await pause(300);
    assert.equal(await evaluate(`localStorage.getItem("netrun-red-scenario-builder-workspace")`), null);
    await click("Scenario / Architecture");
    assert.equal((await json()).id, "new_run");
    await evaluate(`localStorage.setItem("netrun-red-scenario-builder-workspace", "not-json")`);
    await reload();
    assert.equal(await evaluate(`document.getElementById("notice").textContent`), "Saved local workspace is malformed");
    await evaluate(`localStorage.setItem("netrun-red-scenario-builder-workspace", JSON.stringify({ workspaceVersion: 99 }))`);
    await reload();
    assert.equal(await evaluate(`document.getElementById("notice").textContent`), "Saved local workspace is unsupported or malformed");
    checkpoint("New Scenario preserves catalogs; Clear Local Workspace clears all local state");
    assert.deepEqual(consoleErrors, []);
    assert.deepEqual(
      requests.filter((u) => /^https?:/.test(u)),
      [],
    );
    checkpoint(
      "no JS/CSP errors, no HTTP(S) requests, network disabled, laptop layout",
    );
    console.log("Requests:", requests);
    console.log("Artifacts:", artifacts);
    console.log("BROWSER ACCEPTANCE: PASS (automated; no hardware)");
  } finally {
    if (socket && socket.readyState === WebSocket.OPEN) {
      await send("Browser.close", {}, null).catch(() => {});
      socket.close();
    }
    for (const p of pending.values()) p.reject(Error("Browser test finished"));
    pending.clear();
    child.kill();
    console.log("Retained test artifacts:", artifacts);
  }
}
run().catch((e) => {
  console.error(e);
  process.exitCode = 1;
});
