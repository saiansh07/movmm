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
let demoIndex = 0;
let demoPid;
let configSignature;

function syncButtons() {
  operations.forEach(control => { control.disabled = busy || !online; });
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
  if (machineConfig.join() !== configSignature) {
    const form = byId("configuration");
    ["page_size", "frames", "tlb_capacity", "swap_capacity_pages", "fit_policy"].forEach((field, index) => {
      form.elements[field].value = machineConfig[index];
    });
    configSignature = machineConfig.join();
  }

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
  demoIndex = 0;
  demoPid = undefined;
  text("demoStatus", "Starting the demo resets the machine.");
}

const workload = [
  ["Write byte 42 to page 0 and mark it dirty.", () => access("write", demoPid, 0, 42)],
  ["Load page 1 into the second frame.", () => access("translate", demoPid, 256)],
  ["Revisit page 1 for a TLB hit.", () => access("translate", demoPid, 256)],
  ["Load page 2, evict page 0, and write it to swap.", () => access("translate", demoPid, 512)],
  ["Read page 0 back from swap and recover byte 42.", () => access("read", demoPid, 0)]
];

async function demoStep() {
  if (demoIndex === 0) {
    await post("config", {page_size: 256, ram_size_bytes: 512, tlb_capacity: 4, swap_capacity_pages: 8, fit_policy: state.machine.fit_policy, replacement_algo: byId("policy").value});
    demoPid = (await command("processes")).pid;
    text("demoStatus", "Created PID " + demoPid + " with two physical frames.");
  } else {
    const [description, operation] = workload[demoIndex - 1];
    await operation();
    text("demoStatus", description);
  }
  demoIndex++;
}

byId("demo").onclick = () => perform(async () => {
  resetDemo();
  while (demoIndex <= workload.length && online) {
    await demoStep();
    await new Promise(resolve => setTimeout(resolve, 650));
  }
});
byId("step").onclick = () => perform(async () => {
  if (demoIndex > workload.length) resetDemo();
  await demoStep();
});
byId("reset").onclick = () => perform(async () => {
  await post("reset");
  resetDemo();
  text("accessResult", "Create a process to access memory.");
});
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

      const response = await fetch("/api/v1/state", {signal: AbortSignal.timeout(10000)});
      if (!response.ok) throw new Error("Could not read comparison metrics.");
      const {mmu, paging, swap} = await response.json();
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
