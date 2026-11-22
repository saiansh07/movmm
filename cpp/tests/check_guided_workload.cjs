// Run against a dedicated MoVMM server: node cpp/tests/check_guided_workload.cjs http://127.0.0.1:8000
const assert = require("node:assert/strict");
const {readFileSync} = require("node:fs");
const {join} = require("node:path");
const vm = require("node:vm");
const {setTimeout: delay} = require("node:timers/promises");

const base = new URL(process.argv[2]);
const elements = new Map();
const requests = [];
let socket;

class Element {
  value = "";
  children = [];
  dataset = {};
  style = {};
  append(...nodes) { this.children.push(...nodes.flatMap(node => node.fragment ? node.children : [node])); }
  replaceChildren(...nodes) { this.children = []; this.append(...nodes); }
  insertCell() { const cell = new Element(); this.append(cell); return cell; }
  get options() { return this.children; }
  querySelectorAll() { return []; }
  querySelector() { return new Element(); }
}

function element(id) {
  if (!elements.has(id)) elements.set(id, new Element());
  return elements.get(id);
}

const form = element("configuration");
form.elements = Object.fromEntries(
  ["page_size", "frames", "tlb_capacity", "swap_capacity_pages", "fit_policy"].map(name => [name, new Element()])
);
element("demoMode").value = "current";
element("eventFilter").value = "all";

const context = {
  document: {
    getElementById: element,
    querySelectorAll: () => ["demo", "step", "demoMode"].map(element),
    createElement: () => new Element(),
    createDocumentFragment: () => Object.assign(new Element(), {fragment: true})
  },
  Node: Element,
  Option: class { constructor(label, value) { this.value = String(value); } },
  FormData: class { constructor(form) { this.form = form; } get(name) { return this.form.elements[name].value; } },
  location: base,
  WebSocket: class extends WebSocket { constructor(url) { super(url); socket = this; } },
  fetch: (path, options) => {
    if (options?.method === "POST") requests.push(path);
    return fetch(new URL(path, base), options);
  },
  crypto, AbortSignal,
  // Skip animation and keep reconnect timers from holding the test process open.
  setTimeout: (callback, ms) => setTimeout(callback, ms === 1500 ? ms : 0).unref()
};
vm.runInNewContext(readFileSync(join(__dirname, "../../frontend/app.js"), "utf8"), context);

async function until(check) {
  const deadline = Date.now() + 5000;
  while (!check()) {
    assert.ok(Date.now() < deadline, element("notice").textContent || "Timed out waiting for dashboard");
    await delay(20);
  }
}

async function snapshot() {
  const response = await fetch(new URL("/api/v1/state", base));
  assert.ok(response.ok);
  return response.json();
}

async function apply(frames, pageSize, tlb, swap) {
  const values = {page_size: pageSize, frames, tlb_capacity: tlb, swap_capacity_pages: swap, fit_policy: "best_fit"};
  for (const [name, value] of Object.entries(values)) form.elements[name].value = String(value);
  form.oninput();
  form.onsubmit({preventDefault() {}, currentTarget: form});
  await until(() => !element("demo").disabled && element("ramMetric").textContent === "0 / " + frames);
  for (const [name, value] of Object.entries(values)) assert.equal(String(form.elements[name].value), String(value));
  assert.equal(element("demoMode").value, "current");
}

async function verify(frames, pageSize, tlb, swap, lookups, faults, replacements, pageOuts, pageIns) {
  const {machine, mmu, paging, swap: disk, frames: map} = await snapshot();
  assert.equal(machine.total_frames, frames);
  assert.equal(machine.page_size, pageSize);
  assert.equal(machine.fit_policy, "best_fit");
  assert.equal(machine.total_swap_slots, swap);
  assert.equal(mmu.tlb_capacity, tlb);
  assert.equal(mmu.tlb_lookups, lookups);
  assert.equal(mmu.tlb_hits, tlb && lookups > frames ? 1 : 0);
  assert.equal(paging.page_faults, faults);
  assert.equal(paging.replacements, replacements);
  assert.equal(disk.page_outs, pageOuts);
  assert.equal(disk.page_ins, pageIns);
  const resident = Math.min(frames, faults);
  assert.equal(map.length, frames);
  assert.equal(map.filter(frame => frame.resident).length, resident);
  await until(() => element("faultMetric").textContent === faults &&
    element("ramMetric").textContent === resident + " / " + frames);
  assert.equal(element("frames").children.length, frames);
  assert.equal(element("frames").children.filter(cell => cell.className !== "cell free").length, resident);
}

async function workload(mode, frames, pageSize, tlb, swap, button) {
  element("demoMode").value = mode;
  element("demoMode").onchange();
  requests.length = 0;
  const total = frames + Number(Boolean(tlb)) + 1 + Number(Boolean(swap));
  if (button === "step") {
    for (let count = 1; count <= total; count++) {
      await element(button).onclick();
      const faults = count - Number(Boolean(tlb) && count > frames);
      const replacements = Math.max(0, faults - frames);
      await verify(frames, pageSize, tlb, swap, count, faults, replacements,
        Number(Boolean(swap) && replacements > 0), Number(Boolean(swap) && replacements > 1));
    }
    assert.equal(element("step").disabled, true);
  } else {
    await element(button).onclick();
    await verify(frames, pageSize, tlb, swap, total, frames + 1 + Number(Boolean(swap)),
      1 + Number(Boolean(swap)), Number(Boolean(swap)), Number(Boolean(swap)));
  }
  const startup = mode === "preset" ? "/api/v1/config" : "/api/v1/reset";
  assert.deepEqual(requests.filter(path => /\/(reset|config)$/.test(path)), [startup]);
  assert.match(element("demoStatus").textContent, /Workload complete/);
  if (swap) assert.match(element("accessResult").textContent, /byte 42/);
}

(async () => {
  try {
    await until(() => element("connection").dataset.status === "live");
    for (const frames of [2, 4, 8]) {
      await apply(frames, 1024, 2, 3);
      for (const button of ["demo", "step"]) await workload("current", frames, 1024, 2, 3, button);
    }
    for (const button of ["demo", "step"]) {
      await apply(8, 1024, 2, 3);
      await workload("preset", 2, 256, 4, 8, button);
    }
    await apply(1, 4096, 0, 0);
    assert.match(element("demoLimits").textContent, /0 TLB entries.*0 swap slots/);
    await workload("current", 1, 4096, 0, 0, "step");
    await apply(16, 4096, 1, 1);
    await workload("current", 16, 4096, 1, 1, "demo");
    assert.equal((await snapshot()).processes.length, 2);
    console.log("PASS: Run demo and Step with 2, 4, and 8 frames, preset, disabled TLB/swap, and multi-process replacement");
  } finally {
    socket.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
