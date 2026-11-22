"use strict";

const byId = id => document.getElementById(id);
const text = (id, value) => { byId(id).textContent = value; };
const hex = value => "0x" + value.toString(16).toUpperCase().padStart(4, "0");
const dash = "—";
const rate = (count, total) => total ? (100 * count / total).toFixed(1) + "%" : dash;
const operations = document.querySelectorAll("[data-operation]");
const processControl = byId("process");
let state;
let online = false;
let busy = false;
let demo;
let configSignature;
let editingConfig = false;

function syncButtons() {
  operations.forEach(control => { control.disabled = busy || !online; });
  if (demo && demo.next === demo.steps.length) byId("step").disabled = true;
  for (const field of byId("configuration").querySelectorAll("input, select")) {
    field.disabled = busy;
  }
  for (const control of [byId("terminate"), ...byId("access").querySelectorAll("button"), byId("heap").querySelector("button")]) {
    control.disabled ||= !processControl.value;
  }
  byId("heapBlocks").querySelectorAll("button").forEach(button => {
    button.disabled = busy || !online;
  });
}

function connection(label, live) {
  online = live;
  byId("connection").dataset.status = live ? "live" : "offline";
  text("connection", label);
  syncButtons();
}

function notice(message = "") {
  text("notice", message);
  byId("notice").hidden = !message;
}

function cell(label, detail, tone, small = false) {
  const item = document.createElement("div");
  item.className = "cell " + tone + (small ? " virtual-cell" : "");
  item.textContent = label;
  item.title = detail;
  if (!small) {
    const meta = document.createElement("small");
    meta.textContent = detail;
    item.append(meta);
  }
  return item;
}

function empty(message) {
  const paragraph = document.createElement("p");
  paragraph.className = "empty";
  paragraph.textContent = message;
  return paragraph;
}

function table(id, rows, columns, emptyMessage) {
  const body = byId(id);
  const fragment = document.createDocumentFragment();
  for (const values of rows) {
    const row = document.createElement("tr");
    for (const value of values) {
      const column = row.insertCell();
      if (value instanceof Node) column.append(value);
      else column.textContent = value ?? dash;
    }
    fragment.append(row);
  }
  if (!rows.length) {
    const row = document.createElement("tr");
    const column = row.insertCell();
    column.colSpan = columns;
    column.append(empty(emptyMessage));
    fragment.append(row);
  }
  body.replaceChildren(fragment);
}

function selectProcess(pid) {
  if (Array.from(processControl.options).some(option => option.value === String(pid))) {
    processControl.value = pid;
  }
  renderPages();
  renderHeap();
  syncButtons();
}

function renderHeap() {
  const blocks = state?.heap.blocks ?? [];
  const map = document.createDocumentFragment();
  const rows = blocks.map(block => {
    const range = hex(block.offset) + "–" + hex(block.offset + block.size - 1);
    const owner = block.owner == null ? "Free" : "PID " + block.owner;
    const segment = document.createElement("span");
    segment.className = block.owner == null ? "free" : "occupied";
    segment.style.flexGrow = block.size;
    segment.title = range + " · " + owner + " · " + block.size + " B";
    map.append(segment);

    let action;
    if (block.owner != null && String(block.owner) === processControl.value) {
      action = document.createElement("button");
      action.className = "secondary";
      action.textContent = "Free";
      action.setAttribute("aria-label", "Free heap block " + hex(block.offset));
      action.onclick = () => perform(() => command("memory/free", {pid: block.owner, address: block.offset}));
    }
    return [range, block.size + " B", owner, action];
  });
  byId("heapMap").replaceChildren(map);
  table("heapBlocks", rows, 4, "Waiting for engine heap blocks.");
}

function renderPages() {
  const process = state?.processes.find(item => String(item.pid) === processControl.value);
  text("processLabel", process ? "PID " + process.pid : "No process selected");
  if (!process) {
    byId("virtualPages").replaceChildren(empty("Create a process to inspect its address space."));
    table("pageTable", [], 6, "No page table to display.");
    return;
  }
  const size = state.machine.page_size;
  const entries = new Map(process.pages.map(page => [page.page, page]));
  const status = page => page?.present ? "Resident" : page?.swap_slot != null ? "In swap" : page ? "Nonresident" : "Unmapped";
  const tone = page => page?.dirty ? "dirty" : page?.present ? "occupied" : page?.swap_slot != null ? "swapped" : "unused";
  const pages = document.createDocumentFragment();
  for (let vpn = 0; vpn < 65536 / size; vpn++) {
    const page = entries.get(vpn);
    pages.append(cell(vpn, "Page " + vpn + " · " + hex(vpn * size) + " · " + status(page), tone(page), true));
  }
  byId("virtualPages").replaceChildren(pages);
  table("pageTable", process.pages.map(page => [
    page.page, hex(page.page * size) + "–" + hex((page.page + 1) * size - 1), status(page), page.frame, page.swap_slot,
    [page.writable === false && "Read-only", page.referenced && "Referenced", page.dirty && "Dirty"].filter(Boolean).join(", ") || dash
  ]), 6, "This process has not accessed any virtual pages.");
}

function eventGroup(kind) {
  if (kind === "page_in" || kind === "page_out") return "swap";
  if (kind.startsWith("page_")) return "paging";
  if (kind.startsWith("process_")) return "process";
  return "other";
}

function renderEvents() {
  const filter = byId("eventFilter").value;
  const events = (state?.events ?? []).filter(event => filter === "all" || eventGroup(event.event_type) === filter);
  const fragment = document.createDocumentFragment();
  for (const event of events.slice(-100).reverse()) {
    const row = document.createElement("article");
    const name = document.createElement("strong");
    name.className = event.success ? eventGroup(event.event_type) : "failed";
    name.textContent = event.event_type.replaceAll("_", " ");
    const detail = document.createElement("div");
    detail.textContent = event.explanation;
    const meta = document.createElement("small");
    const parts = ["Step " + event.step_number];
    if (event.process_id) parts.push("PID " + event.process_id);
    if (["paging", "swap"].includes(eventGroup(event.event_type))) {
      parts.push("Page " + event.page_number, "Frame " + event.frame_number);
    }
    if (eventGroup(event.event_type) === "swap") parts.push("Slot " + event.swap_slot);
    meta.textContent = parts.join(" · ");
    detail.append(meta);
    row.append(name, detail);
    fragment.append(row);
  }
  byId("events").replaceChildren(events.length ? fragment : empty("No matching events."));
}

function render(snapshot) {
  if (!Array.isArray(snapshot.processes) || !Array.isArray(snapshot.frames)) {
    throw new Error("This engine needs to be rebuilt with Phase 5. Restart it through run.ps1.");
  }
  state = snapshot;
  const {machine, paging, mmu, swap, heap} = snapshot;
  const hits = mmu.tlb_hits;
  const misses = mmu.tlb_misses;
  text("ramMetric", paging.resident_pages + " / " + machine.total_frames);
  text("ramDetail", machine.page_size + " B per frame");
  text("tlbMetric", rate(hits, hits + misses));
  text("liveRates", "Page fault rate " + rate(paging.page_faults, mmu.tlb_lookups) +
    " · Allocation success rate " + rate(heap.total_allocations, heap.allocation_attempts));
  text("tlbDetail", hits + " hits · " + misses + " misses");
  text("faultMetric", paging.page_faults);
  text("replacementDetail", paging.replacements + " replacements · " + (paging.lru_enabled ? "LRU" : "FIFO"));
  text("swapMetric", swap.page_outs + " out / " + swap.page_ins + " in");
  text("swapDetail", swap.used_slots + " slots occupied");
  text("swapSlots", swap.used_slots + " of " + machine.total_swap_slots + " slots occupied");
  byId("swapProgress").max = machine.total_swap_slots || 1;
  byId("swapProgress").value = swap.used_slots;
  text("swapIO", swap.page_outs + " page writes · " + swap.page_ins + " page reads");
  text("heapSummary", heap.allocated_bytes + " B used · " + heap.free_bytes + " B free");
  text("heapDetails", heap.total_allocations == null ? "Heap metrics require the Phase 6 engine." :
    heap.total_allocations + " allocations · " + heap.total_frees + " frees · " +
    heap.external_fragmentation_bytes + " B external fragmentation · " +
    heap.internal_fragmentation_bytes + " B internal fragmentation");
  text("frameSummary", (machine.total_frames - paging.resident_pages) + " free");
  text("tlbSummary", mmu.entries.length + " / " + mmu.tlb_capacity + " cached");
  byId("policy").value = paging.lru_enabled ? "lru" : "fifo";
  text("policyDescription", paging.lru_enabled ? "Evicts the least recently accessed page." : "Evicts the page that entered RAM first.");
  const describeFrame = number => {
    const frame = snapshot.frames[number];
    return frame ? "F" + number + " / P" + frame.pid + " / V" + frame.page : dash;
  };
  table("evictionOrder", (paging.fifo_order ?? []).map((frame, rank) => [
    rank + 1, describeFrame(frame), describeFrame(paging.lru_order[rank])
  ]), 3, "No resident frames.");
  table("swapSlotsTable", (swap.slots ?? []).map(slot => [
    slot.slot, slot.pid, slot.page, slot.pid == null ? "Free" : slot.resident ? "RAM + swap" : "In swap"
  ]), 4, "No swap slots configured.");

  const machineConfig = [machine.page_size, machine.total_frames, mmu.tlb_capacity, machine.total_swap_slots, machine.fit_policy];
  if (!editingConfig && machineConfig.join() !== configSignature) {
    const form = byId("configuration");
    ["page_size", "frames", "tlb_capacity", "swap_capacity_pages", "fit_policy"].forEach((field, index) => {
      form.elements[field].value = machineConfig[index];
    });
    configSignature = machineConfig.join();
  }
  updateDemoLimits();

  const frameCells = document.createDocumentFragment();
  for (const frame of snapshot.frames) {
    const detail = frame.resident ? "P" + frame.pid + " · V" + frame.page : "Free";
    frameCells.append(cell("F" + frame.frame, detail, frame.dirty ? "dirty" : frame.resident ? "occupied" : "free"));
  }
  byId("frames").replaceChildren(frameCells);
  const chosen = processControl.value;
  const processes = [...snapshot.processes].sort((left, right) => left.pid - right.pid);
  processControl.replaceChildren(...(processes.length ? processes.map(({pid}) => new Option("PID " + pid, pid)) : [new Option("No processes", "")]));
  selectProcess(chosen);
  table("tlbTable", mmu.entries.map(entry => [entry.slot, "PID " + entry.pid, entry.page, entry.frame, entry.dirty ? "Yes" : "No"]), 5, "No cached translations.");
  renderEvents();
}

function connect() {
  const protocol = location.protocol === "https:" ? "wss:" : "ws:";
  const socket = new WebSocket(protocol + "//" + location.host + "/ws/simulation");
  socket.onmessage = event => {
    try {
      const message = JSON.parse(event.data);
      if (message.type === "error") throw new Error(message.error);
      if (message.type === "initial_state" || message.type === "state_update") {
        if (!online) notice();
        render(message.data);
        connection("Engine live", true);
      }
    } catch (error) {
      connection("Engine unavailable", false);
      notice(error.message);
    }
  };
  socket.onclose = () => {
    connection("Reconnecting", false);
    setTimeout(connect, 1500);
  };
}

async function post(route, body) {
  const response = await fetch("/api/v1/" + route, {
    method: "POST",
    headers: {"Content-Type": "application/json"},
    body: JSON.stringify(body),
    signal: AbortSignal.timeout(10000)
  });
  const payload = await response.json();
  if (!response.ok) {
    const detail = payload.detail;
    throw new Error(typeof detail === "string" ? detail : detail?.[0]?.msg ?? "Request failed (" + response.status + ")");
  }
  return payload;
}

function command(route, params = {}) {
  return post(route, {id: crypto.randomUUID(), params});
}

async function readState() {
  const response = await fetch("/api/v1/state", {signal: AbortSignal.timeout(10000)});
  if (!response.ok) throw new Error("Could not read the machine state.");
  return response.json();
}

async function perform(work) {
  if (busy || !online) return;
  busy = true;
  notice();
  syncButtons();
  try {
    await work();
  } catch (error) {
    notice(error.message);
  } finally {
    busy = false;
    syncButtons();
  }
}

function parseAddress(input) {
  const value = input.trim();
  if (!/^(?:0x[0-9a-f]+|[0-9]+)$/i.test(value) || Number(value) > 65535) {
    throw new Error("Enter a virtual address from 0 to 65535 or 0x0000 to 0xFFFF.");
  }
  return Number(value);
}

async function access(operation, pid, address, byte) {
  const params = {pid, virtual_address: address};
  if (operation === "write") params.data = [byte];
  if (operation === "read") params.size = 1;
  const result = await command("memory/" + operation, params);
  const prefix = hex(address);
  if (operation === "translate") {
    const addressParts = "page " + result.virtual_page + " · offset " + result.page_offset;
    const outcome = result.protection_fault ? "Protection fault" : "frame " + result.physical_frame +
      " · physical " + hex(result.physical_address) +
      " · " + (result.tlb_hit ? "TLB hit" : "TLB miss") + (result.page_fault ? " · page fault handled" : "");
    text("accessResult", prefix + " · " + addressParts + " → " + outcome);
  } else {
    text("accessResult", operation === "read" ? prefix + " → byte " + result.bytes[0] : "Wrote byte " + byte + " to " + prefix);
  }
}

function resetDemo() {
  demo = undefined;
  text("demoStatus", "Ready to start.");
  updateDemoLimits();
  syncButtons();
}

const usingPreset = () => byId("demoMode").value === "preset";

function updateDemoLimits() {
  const preset = usingPreset();
  text("demoConfig", preset
    ? "Starting this preset applies 2 frames, 256 B pages, 4 TLB entries, and 8 swap slots."
    : "A new workload resets memory using the applied machine settings.");
  const limits = [];
  if (!preset && state) {
    if (state.mmu.tlb_capacity === 0) limits.push("TLB hits are unavailable with 0 TLB entries.");
    if (state.machine.total_swap_slots === 0) limits.push("Swap-out and swap-in are unavailable with 0 swap slots. The demo evicts a clean page.");
  }
  text("demoLimits", limits.join(" "));
}

function makeWorkload({machine, mmu}) {
  const {total_frames: frames, page_size: pageSize, total_swap_slots: swapSlots} = machine;
  const pagesPerProcess = 65536 / pageSize;
  const steps = [];
  const add = (operation, page, description) => steps.push({
    operation, process: Math.floor(page / pagesPerProcess),
    address: (page % pagesPerProcess) * pageSize, description
  });

  if (swapSlots) add("write", 0, "Write byte 42 to the first page and mark it dirty.");
  else add("translate", 0, "Load the first page with a page fault.");
  for (let page = 1; page < frames; page++) {
    add("translate", page, "Load another page into a free frame.");
  }
  // Revisit the newest page so the first page remains the victim under FIFO and LRU.
  if (mmu.tlb_capacity) add("translate", frames - 1, "Revisit the most recently loaded page for a TLB hit.");
  add("translate", frames, swapSlots
    ? "Load one more page, evict the first page, and write it to swap."
    : "Load one more page and evict the first, clean page.");
  if (swapSlots) add("read", 0, "Read the first page back from swap.");
  return steps;
}

async function startDemo() {
  if (usingPreset()) {
    const {machine, paging} = await readState();
    await post("config", {
      page_size: 256, ram_size_bytes: 512, tlb_capacity: 4, swap_capacity_pages: 8,
      fit_policy: machine.fit_policy, replacement_algo: paging.lru_enabled ? "lru" : "fifo"
    });
  } else await post("reset");
  demo = {steps: makeWorkload(await readState()), next: 0, pids: []};
}

async function demoStep() {
  if (!demo) await startDemo();
  if (demo.next >= demo.steps.length) return;
  const {operation, process, address, description} = demo.steps[demo.next];
  demo.pids[process] ??= (await command("processes")).pid;
  await access(operation, demo.pids[process], address, 42);
  demo.next++;
  const progress = "Operation " + demo.next + " of " + demo.steps.length + ". " + description;
  text("demoStatus", demo.next === demo.steps.length
    ? progress + " Workload complete. Use Run demo or reset the engine to start again."
    : progress);
}

byId("demoMode").onchange = resetDemo;
byId("demo").onclick = () => perform(async () => {
  resetDemo();
  do {
    await demoStep();
    if (demo.next < demo.steps.length) {
      await new Promise(resolve => setTimeout(resolve, usingPreset() ? 650 : 40));
    }
  } while (online && demo.next < demo.steps.length);
});
byId("step").onclick = () => perform(demoStep);
byId("reset").onclick = () => perform(async () => {
  await post("reset");
  resetDemo();
  text("accessResult", "Create a process to access memory.");
});
byId("configuration").oninput = () => { editingConfig = true; };
byId("configuration").onsubmit = event => {
  event.preventDefault();
  const form = new FormData(event.currentTarget);
  perform(async () => {
    const pageSize = Number(form.get("page_size"));
    await post("config", {
      page_size: pageSize,
      ram_size_bytes: pageSize * Number(form.get("frames")),
      tlb_capacity: Number(form.get("tlb_capacity")),
      swap_capacity_pages: Number(form.get("swap_capacity_pages")),
      fit_policy: form.get("fit_policy"),
      replacement_algo: byId("policy").value
    });
    editingConfig = false;
    byId("demoMode").value = "current";
    resetDemo();
    text("accessResult", "Create a process to access memory.");
  });
};
byId("create").onclick = () => perform(() => command("processes"));
byId("terminate").onclick = () => perform(() => post("processes/" + processControl.value));
processControl.onchange = () => selectProcess(processControl.value);
byId("policy").onchange = () => perform(() => command("policies", {policy_type: "replacement", policy: byId("policy").value}));
byId("access").onsubmit = event => {
  event.preventDefault();
  const form = new FormData(event.currentTarget);
  perform(async () => {
    const operation = event.submitter?.value ?? "translate";
    const pid = Number(processControl.value);
    const address = parseAddress(form.get("address"));
    if (operation === "readonly" || operation === "writable") {
      const writable = operation === "writable";
      await post("memory/permissions", {pid, virtual_address: address, writable});
      text("accessResult", hex(address) + (writable ? " is writable." : " is read-only."));
      return;
    }
    await access(operation, pid, address, Number(form.get("byte")));
  });
};
byId("heap").onsubmit = event => {
  event.preventDefault();
  const form = new FormData(event.currentTarget);
  perform(() => command("memory/allocate", {pid: Number(processControl.value), size: Number(form.get("size"))}));
};
byId("compare").onclick = () => perform(async () => {
  resetDemo();
  const results = [];
  table("comparisonResults", results, 6, "Running workload.");
  try {
    for (const algorithm of ["fifo", "lru"]) {
      text("comparisonStatus", "Running " + algorithm.toUpperCase() + " on two frames…");
      await post("config", {
        page_size: 256, ram_size_bytes: 512, tlb_capacity: 4, swap_capacity_pages: 8,
        replacement_algo: algorithm
      });
      const {pid} = await command("processes");
      await access("write", pid, 0, 42);
      await access("translate", pid, 256);
      await access("translate", pid, 0);
      await access("translate", pid, 512);
      await access("read", pid, 0);

      const {mmu, paging, swap} = await readState();
      results.push([algorithm.toUpperCase(), rate(mmu.tlb_hits, mmu.tlb_lookups),
        paging.page_faults, paging.replacements, swap.page_outs, swap.page_ins]);
      table("comparisonResults", results, 6, "");
    }
    text("comparisonStatus", "Both runs completed. The memory map shows the LRU run.");
  } catch (error) {
    text("comparisonStatus", "Comparison interrupted.");
    throw error;
  }
});
byId("eventFilter").onchange = renderEvents;
table("comparisonResults", [], 6, "Run the comparison to collect measurements.");
table("pageTable", [], 6, "No page table to display.");
table("tlbTable", [], 5, "No cached translations.");
renderHeap();
syncButtons();
connect();
