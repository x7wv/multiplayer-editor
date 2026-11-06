// Multiplayer Editor — relay server
// Made by x7wv
//
// One process holds every room. Each room keeps the authoritative copy of the
// level (object id -> save string), an op log for the timeline / per-player undo,
// and all the social state (chat, tasks, votes, timer, drawings...).

"use strict";
const http = require("http");
const fs = require("fs");
const path = require("path");
const crypto = require("crypto");
const { WebSocketServer } = require("ws");

const PORT = Number(process.env.PORT || 8080);
const DATA_DIR = process.env.DATA_DIR || path.join(__dirname, "data");
const PROTOCOL = 1;
const MAX_LOG = Number(process.env.MAX_LOG || 250000);     // ops kept per room for timeline/undo
const ROOM_TTL_MS = 7 * 24 * 3600 * 1000;                    // empty rooms deleted after 7 days
const AFK_MS = 2 * 60 * 1000;
const MAX_PLAYERS = Number(process.env.MAX_PLAYERS || 64);
const ROLES = ["viewer", "decorator", "builder", "host"];
const RANK = { viewer: 0, decorator: 1, builder: 2, host: 3 };

fs.mkdirSync(DATA_DIR, { recursive: true });

// ---------------------------------------------------------------- helpers
const now = () => Date.now();
const rid = (n = 6) => {
  const a = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  let s = "";
  for (const b of crypto.randomBytes(n)) s += a[b % a.length];
  return s;
};
const str = (v, max = 200) => (typeof v === "string" ? v.slice(0, max) : "");
const num = (v, d = 0) => (typeof v === "number" && isFinite(v) ? v : d);
const hashPw = (pw) => (pw ? crypto.createHash("sha256").update("me:" + pw).digest("hex") : "");

// GD save strings are "key,value,key,value". Key 57 = groups ("1.2.3").
function groupsOf(s) {
  if (!s) return [];
  const p = s.split(",");
  for (let i = 0; i + 1 < p.length; i += 2) {
    if (p[i] === "57") return p[i + 1].split(".").map(Number).filter((n) => n > 0);
  }
  return [];
}

const ACHIEVEMENTS = [
  { id: "first_block", name: "First Block", test: (s) => s.placed >= 1 },
  { id: "builder_100", name: "Getting Started (100 objects)", test: (s) => s.placed >= 100 },
  { id: "builder_1k", name: "Architect (1,000 objects)", test: (s) => s.placed >= 1000 },
  { id: "builder_10k", name: "Megabuilder (10,000 objects)", test: (s) => s.placed >= 10000 },
  { id: "builder_50k", name: "Absolute Unit (50,000 objects)", test: (s) => s.placed >= 50000 },
  { id: "eraser", name: "Eraser (delete 1,000)", test: (s) => s.deleted >= 1000 },
  { id: "chatty", name: "Chatterbox (100 messages)", test: (s) => s.chats >= 100 },
  { id: "hour", name: "Dedicated (1 hour in a room)", test: (s) => s.timeMs >= 3600e3 },
  { id: "tenhours", name: "No Life (10 hours)", test: (s) => s.timeMs >= 36000e3 },
  { id: "tasker", name: "Task Crusher (finish 10 tasks)", test: (s) => s.tasksDone >= 10 },
];

// ---------------------------------------------------------------- room
class Room {
  constructor(o) {
    this.code = o.code;
    this.name = str(o.name, 40) || "Untitled Collab";
    this.ownerUid = o.ownerUid;
    this.pw = o.pw || "";
    this.public = !!o.public;
    this.tags = (o.tags || []).slice(0, 5).map((t) => str(t, 16));
    this.motd = str(o.motd, 300);
    this.approval = !!o.approval;
    this.slowMs = 0;
    this.rate = 400;           // max object ops per player per second
    this.frozen = false;
    this.finished = false;
    this.defaultRole = "builder";
    this.objects = new Map();  // id -> {s, a (author uid), k (kind)}
    this.settings = str(o.settings, 200000);
    this.seq = 0;
    this.log = [];             // {q seq, t time, a uid, ops:[{o,id,s,p (prev)}]}
    this.roles = {};           // uid -> role
    this.bans = new Set();
    this.names = {};           // uid -> last known display name (for credits/heatmap)
    this.stats = {};           // uid -> stats
    this.ach = {};             // uid -> [ids]
    this.locks = new Set();
    this.tasks = [];
    this.votes = [];
    this.drawings = [];
    this.templates = [];
    this.groupClaims = {};     // group id -> uid
    this.timer = { running: false, start: 0, acc: 0 };
    this.chat = [];
    this.created = now();
    this.lastActive = now();
    this.players = new Map();  // pid -> client (live only)
    this.pending = new Map();  // pid -> client awaiting approval
    this.feedBuf = {};         // uid -> {add, del, upd}
    this.dirty = true;
  }

  roleOf(uid) {
    if (uid === this.ownerUid) return "host";
    return this.roles[uid] || this.defaultRole;
  }
  statsOf(uid) {
    return (this.stats[uid] ||= { placed: 0, deleted: 0, edited: 0, chats: 0, timeMs: 0, tasksDone: 0 });
  }
  info() {
    return {
      code: this.code, name: this.name, public: this.public, tags: this.tags,
      players: this.players.size, locked: !!this.pw, approval: this.approval,
      objects: this.objects.size, finished: this.finished,
      host: this.names[this.ownerUid] || "?",
    };
  }
  settingsInfo() {
    return {
      name: this.name, motd: this.motd, slowMs: this.slowMs, rate: this.rate,
      frozen: this.frozen, finished: this.finished, public: this.public,
      tags: this.tags, approval: this.approval, defaultRole: this.defaultRole,
      locked: !!this.pw,
    };
  }
  broadcast(msg, except) {
    const data = JSON.stringify(msg);
    for (const c of this.players.values()) if (c !== except) c.sendRaw(data);
  }
  playerInfo(c) {
    const s = this.statsOf(c.uid);
    return {
      pid: c.pid, uid: c.uid, name: c.name, nick: c.nick, color: c.color, icon: c.icon,
      role: this.roleOf(c.uid), afk: c.afk, playing: c.playing, ping: c.lastPing,
      placed: s.placed, ach: this.ach[c.uid] || [],
    };
  }
  snapshot() {
    const objects = [];
    for (const [id, o] of this.objects) objects.push([id, o.s, o.a]);
    return {
      code: this.code, settings: this.settings, objects, seq: this.seq,
      room: this.settingsInfo(), locks: [...this.locks], tasks: this.tasks,
      drawings: this.drawings, timer: this.timer, now: now(), names: this.names,
      chat: this.chat.slice(-50), players: [...this.players.values()].map((c) => this.playerInfo(c)),
      leaderboard: this.leaderboard(),
      votes: this.votes.map((v) => ({ ...v, yes: v.yes.length, no: v.no.length })),
      tpls: this.templates.map(({ id, name, by, count, ts }) => ({ id, name, by, count, ts })),
    };
  }
  leaderboard() {
    return Object.entries(this.stats)
      .map(([uid, s]) => ({ uid, name: this.names[uid] || "?", placed: s.placed, deleted: s.deleted, timeMs: s.timeMs, objectsNow: 0 }))
      .sort((a, b) => b.placed - a.placed).slice(0, 50);
  }

  // Apply ops coming from `uid`. Returns {applied, rejected:[{id,s}]}.
  applyOps(uid, ops, { force = false, tag = null } = {}) {
    const role = this.roleOf(uid);
    const applied = [];
    const fix = [];
    for (const op of ops) {
      const id = str(op.id, 40);
      if (!id) continue;
      const cur = this.objects.get(id);
      const kind = op.k === "t" || op.k === "g" || op.k === "d" ? op.k : (cur && cur.k) || "g";
      let ok = force;
      if (!ok) {
        ok = !this.frozen || role === "host";
        if (ok && role === "viewer") ok = false;
        if (ok && role === "decorator" && (kind !== "d" || (cur && cur.k !== "d"))) ok = false;
        if (ok && this.locks.has(id) && role !== "host") ok = false;
      }
      if (!ok) {
        fix.push({ id, s: cur ? cur.s : null });
        continue;
      }
      if (op.o === "add" || op.o === "upd") {
        const s = str(op.s, 20000);
        if (!s) continue;
        const prev = cur ? cur.s : null;
        this.objects.set(id, { s, a: cur && op.o === "upd" ? cur.a : uid, k: kind });
        applied.push({ o: cur ? "upd" : "add", id, s, k: kind, p: prev });
      } else if (op.o === "del") {
        if (!cur) continue;
        this.objects.delete(id);
        this.locks.delete(id);
        applied.push({ o: "del", id, p: cur.s, k: cur.k, pa: cur.a });
      }
    }
    if (applied.length) {
      this.seq++;
      const entry = { q: this.seq, t: now(), a: uid, ops: applied };
      if (tag) entry.tag = tag;
      this.log.push(entry);
      if (this.log.length > MAX_LOG) this.log.splice(0, this.log.length - MAX_LOG);
      const st = this.statsOf(uid);
      const fb = (this.feedBuf[uid] ||= { add: 0, del: 0, upd: 0 });
      for (const a of applied) {
        if (a.o === "add") { st.placed++; fb.add++; }
        else if (a.o === "del") { st.deleted++; fb.del++; }
        else { st.edited++; fb.upd++; }
      }
      this.dirty = true;
      this.lastActive = now();
    }
    return { applied, fix };
  }

  // Builds inverse ops for a set of log entries (newest first).
  inverseOf(entries) {
    const inv = [];
    const seen = new Set();
    for (const e of entries) {
      for (let i = e.ops.length - 1; i >= 0; i--) {
        const op = e.ops[i];
        if (op.o === "add") inv.push({ o: "del", id: op.id });
        else if (op.o === "upd") inv.push({ o: "upd", id: op.id, s: op.p, k: op.k });
        else if (op.o === "del") inv.push({ o: "add", id: op.id, s: op.p, k: op.k });
        seen.add(op.id);
      }
    }
    return inv;
  }

  toJSON() {
    return {
      code: this.code, name: this.name, ownerUid: this.ownerUid, pw: this.pw, public: this.public,
      tags: this.tags, motd: this.motd, approval: this.approval, slowMs: this.slowMs, rate: this.rate,
      frozen: this.frozen, finished: this.finished, defaultRole: this.defaultRole,
      objects: [...this.objects], settings: this.settings, seq: this.seq,
      log: this.log.slice(-20000), roles: this.roles, bans: [...this.bans], names: this.names,
      stats: this.stats, ach: this.ach, locks: [...this.locks], tasks: this.tasks, votes: this.votes,
      drawings: this.drawings, templates: this.templates, groupClaims: this.groupClaims,
      timer: this.timer, chat: this.chat.slice(-200), created: this.created, lastActive: this.lastActive,
    };
  }
  static fromJSON(j) {
    const r = new Room({ code: j.code, name: j.name, ownerUid: j.ownerUid, pw: j.pw, public: j.public, tags: j.tags, motd: j.motd, approval: j.approval, settings: j.settings });
    Object.assign(r, {
      slowMs: j.slowMs || 0, rate: j.rate || 400, frozen: !!j.frozen, finished: !!j.finished,
      defaultRole: j.defaultRole || "builder", objects: new Map(j.objects || []), seq: j.seq || 0,
      log: j.log || [], roles: j.roles || {}, bans: new Set(j.bans || []), names: j.names || {},
      stats: j.stats || {}, ach: j.ach || {}, locks: new Set(j.locks || []), tasks: j.tasks || [],
      votes: j.votes || [], drawings: j.drawings || [], templates: j.templates || [],
      groupClaims: j.groupClaims || {}, timer: j.timer || { running: false, start: 0, acc: 0 },
      chat: j.chat || [], created: j.created || now(), lastActive: j.lastActive || now(),
    });
    r.dirty = false;
    return r;
  }
}

const rooms = new Map();

// load saved rooms
for (const f of fs.readdirSync(DATA_DIR)) {
  if (!f.endsWith(".json")) continue;
  try {
    const r = Room.fromJSON(JSON.parse(fs.readFileSync(path.join(DATA_DIR, f), "utf8")));
    rooms.set(r.code, r);
  } catch (e) { console.error("failed to load", f, e.message); }
}
console.log(`[Multiplayer Editor] loaded ${rooms.size} saved rooms`);

function saveRoom(r) {
  const file = path.join(DATA_DIR, r.code + ".json");
  fs.writeFile(file + ".tmp", JSON.stringify(r), (err) => {
    if (!err) fs.rename(file + ".tmp", file, () => {});
  });
  r.dirty = false;
}

// ---------------------------------------------------------------- client
let nextPid = 1;
class Client {
  constructor(ws, ip) {
    this.ws = ws;
    this.ip = ip;
    this.pid = nextPid++;
    this.uid = null;
    this.name = "Player";
    this.nick = "";
    this.color = [0, 200, 255];
    this.icon = { id: 1, c1: 0, c2: 3, glow: false };
    this.room = null;
    this.afk = false;
    this.playing = false;
    this.lastAct = now();
    this.lastChat = 0;
    this.lastPing = 0;
    this.joinedAt = 0;
    this.opBucket = { t: now(), n: 0 };
    this.msgBucket = { t: now(), n: 0 };
  }
  send(m) { this.sendRaw(JSON.stringify(m)); }
  sendRaw(d) { if (this.ws.readyState === 1) this.ws.send(d); }
  display() { return this.nick || this.name; }
}

// ---------------------------------------------------------------- handlers
function requireRoom(c) { if (!c.room) { c.send({ t: "error", msg: "Not in a room" }); return false; } return true; }
function requireRole(c, min) {
  if (!requireRoom(c)) return false;
  if (RANK[c.room.roleOf(c.uid)] < RANK[min]) { c.send({ t: "error", msg: "You need to be " + min + " for that" }); return false; }
  return true;
}
function findPlayer(room, pid) { return room.players.get(Number(pid)) || room.pending.get(Number(pid)); }

function enterRoom(c, room) {
  room.players.set(c.pid, c);
  room.names[c.uid] = c.display();
  c.room = room;
  c.joinedAt = now();
  room.lastActive = now();
  c.send({ t: "joined", you: c.pid, role: room.roleOf(c.uid), ...room.snapshot() });
  room.broadcast({ t: "player+", p: room.playerInfo(c) }, c);
  room.dirty = true;
}

function leaveRoom(c, reason) {
  const room = c.room;
  if (!room) return;
  room.pending.delete(c.pid);
  if (room.players.delete(c.pid)) {
    room.statsOf(c.uid).timeMs += now() - c.joinedAt;
    room.broadcast({ t: "player-", pid: c.pid, reason: reason || "left" });
  }
  // drop selections held by the leaver
  room.broadcast({ t: "cursor", p: c.pid, gone: true });
  c.room = null;
  room.dirty = true;
}

function checkAchievements(room, c) {
  const s = room.statsOf(c.uid);
  const have = (room.ach[c.uid] ||= []);
  for (const a of ACHIEVEMENTS) {
    if (!have.includes(a.id) && a.test(s)) {
      have.push(a.id);
      room.broadcast({ t: "achievement", p: c.pid, name: c.display(), id: a.id, title: a.name });
    }
  }
}

const H = {};

H.hello = (c, m) => {
  c.uid = str(m.uid, 64) || "anon-" + rid(10);
  c.name = str(m.name, 24) || "Player";
  c.nick = str(m.nick, 24);
  if (Array.isArray(m.color)) c.color = m.color.slice(0, 3).map((v) => Math.max(0, Math.min(255, num(v))));
  if (m.icon && typeof m.icon === "object") c.icon = { id: num(m.icon.id, 1), c1: num(m.icon.c1), c2: num(m.icon.c2, 3), glow: !!m.icon.glow };
  c.send({ t: "welcome", pid: c.pid, protocol: PROTOCOL, motd: "Multiplayer Editor server — made by x7wv" });
};

H.list = (c) => {
  const list = [...rooms.values()].filter((r) => r.public && !r.bans.has(c.uid)).map((r) => r.info())
    .sort((a, b) => b.players - a.players).slice(0, 100);
  c.send({ t: "rooms", list });
};

H.create = (c, m) => {
  if (!c.uid) return c.send({ t: "error", msg: "Say hello first" });
  if (c.room) leaveRoom(c);
  let code; do code = rid(6); while (rooms.has(code));
  const room = new Room({
    code, name: m.name, ownerUid: c.uid, pw: hashPw(str(m.password, 64)), public: m.public,
    tags: Array.isArray(m.tags) ? m.tags : [], motd: m.motd, approval: m.approval, settings: m.settings,
  });
  if (Array.isArray(m.objects)) {
    for (const o of m.objects) {
      if (!Array.isArray(o)) continue;
      const id = str(o[0], 40), s = str(o[1], 20000), k = ["g", "d", "t"].includes(o[2]) ? o[2] : "g";
      if (id && s) room.objects.set(id, { s, a: c.uid, k });
    }
  }
  room.names[c.uid] = c.display();
  rooms.set(code, room);
  enterRoom(c, room);
  saveRoom(room);
};

H.join = (c, m) => {
  if (!c.uid) return c.send({ t: "error", msg: "Say hello first" });
  const room = rooms.get(str(m.code, 12).toUpperCase());
  if (!room) return c.send({ t: "error", msg: "Room not found" });
  if (room.bans.has(c.uid)) return c.send({ t: "error", msg: "You are banned from this room" });
  if (room.players.size >= MAX_PLAYERS) return c.send({ t: "error", msg: "Room is full" });
  const isOwner = room.ownerUid === c.uid;
  if (room.pw && !isOwner && hashPw(str(m.password, 64)) !== room.pw) return c.send({ t: "error", msg: "Wrong password" });
  if (c.room) leaveRoom(c);
  const known = room.roles[c.uid] !== undefined || room.stats[c.uid];
  if (room.approval && !isOwner && !known) {
    room.pending.set(c.pid, c);
    c.room = room;
    c.send({ t: "pending", code: room.code });
    for (const p of room.players.values()) if (room.roleOf(p.uid) === "host") p.send({ t: "joinRequest", pid: c.pid, name: c.display() });
    return;
  }
  enterRoom(c, room);
};

H.approve = (c, m) => {
  if (!requireRole(c, "host")) return;
  const p = c.room.pending.get(Number(m.pid));
  if (!p) return;
  c.room.pending.delete(p.pid);
  if (m.ok) { enterRoom(p, c.room); }
  else { p.room = null; p.send({ t: "kicked", reason: "Host declined your join request" }); }
};

H.leave = (c) => leaveRoom(c, "left");

H.ops = (c, m) => {
  if (!requireRoom(c) || !c.room.players.has(c.pid)) return;
  const room = c.room;
  let ops = Array.isArray(m.ops) ? m.ops.slice(0, 5000) : [];
  // rate limiter (anti-grief). Host is exempt.
  if (room.roleOf(c.uid) !== "host") {
    const b = c.opBucket, t = now();
    if (t - b.t > 1000) { b.t = t; b.n = 0; }
    const allowed = Math.max(0, room.rate - b.n);
    if (ops.length > allowed) {
      const over = ops.slice(allowed);
      ops = ops.slice(0, allowed);
      c.send({ t: "reject", reason: "Slow down! Edit rate limit reached", fix: over.map((o) => { const cur = room.objects.get(o.id); return { id: o.id, s: cur ? cur.s : null }; }) });
    }
    b.n += ops.length;
  }
  const { applied, fix } = room.applyOps(c.uid, ops);
  if (fix.length) c.send({ t: "reject", reason: room.frozen ? "Room is frozen" : "You can't edit that", fix });
  if (applied.length) {
    room.broadcast({ t: "ops", by: c.pid, seq: room.seq, ops: applied.map(({ o, id, s, k }) => ({ o, id, s, k })) }, c);
    c.send({ t: "ack", seq: room.seq, n: m.n });
    checkAchievements(room, c);
  }
};

H.settings = (c, m) => {
  if (!requireRole(c, "builder")) return;
  if (c.room.frozen && c.room.roleOf(c.uid) !== "host") return;
  c.room.settings = str(m.s, 200000);
  c.room.dirty = true;
  c.room.broadcast({ t: "settings", by: c.pid, s: c.room.settings }, c);
};

// High-rate presence: cursor + camera + selection. Not stored.
H.cursor = (c, m) => {
  if (!requireRoom(c) || !c.room.players.has(c.pid)) return;
  const out = { t: "cursor", p: c.pid, x: num(m.x), y: num(m.y), cx: num(m.cx), cy: num(m.cy), z: num(m.z, 1) };
  if (Array.isArray(m.sel)) out.sel = m.sel.slice(0, 2000).map((s) => str(s, 40));
  c.room.broadcast(out, c);
};

H.play = (c, m) => {
  if (!requireRoom(c)) return;
  const on = !!m.on;
  if (on !== c.playing) { c.playing = on; c.room.broadcast({ t: "player~", p: { pid: c.pid, playing: on } }); }
  c.room.broadcast({ t: "play", p: c.pid, on, x: num(m.x), y: num(m.y), r: num(m.r), m: num(m.m), d: !!m.d, s: num(m.s, 1), f: !!m.f }, c);
};

H.chat = (c, m) => {
  if (!requireRoom(c) || !c.room.players.has(c.pid)) return;
  const room = c.room, text = str(m.text, 300).trim();
  if (!text) return;
  if (room.slowMs && room.roleOf(c.uid) !== "host" && now() - c.lastChat < room.slowMs)
    return c.send({ t: "error", msg: `Slow mode: wait ${Math.ceil((room.slowMs - (now() - c.lastChat)) / 1000)}s` });
  c.lastChat = now();
  const msg = { p: c.pid, name: c.display(), color: c.color, text, ts: now() };
  room.chat.push(msg);
  if (room.chat.length > 500) room.chat.splice(0, 100);
  room.statsOf(c.uid).chats++;
  room.broadcast({ t: "chat", ...msg });
  checkAchievements(room, c);
};

H.typing = (c, m) => { if (requireRoom(c)) c.room.broadcast({ t: "typing", p: c.pid, on: !!m.on }, c); };
H.react = (c, m) => { if (requireRoom(c)) c.room.broadcast({ t: "react", p: c.pid, e: str(m.e, 16), x: num(m.x), y: num(m.y) }); };
H.ping = (c, m) => { if (requireRoom(c)) c.room.broadcast({ t: "ping", p: c.pid, name: c.display(), color: c.color, x: num(m.x), y: num(m.y), label: str(m.label, 60) }); };

H.draw = (c, m) => {
  if (!requireRole(c, "decorator")) return;
  const pts = Array.isArray(m.pts) ? m.pts.slice(0, 2000).map((v) => num(v)) : [];
  if (pts.length < 4) return;
  const d = { id: rid(8), p: c.pid, uid: c.uid, c: Array.isArray(m.c) ? m.c.slice(0, 3) : c.color, w: num(m.w, 3), pts };
  c.room.drawings.push(d);
  if (c.room.drawings.length > 500) c.room.drawings.shift();
  c.room.dirty = true;
  c.room.broadcast({ t: "draw", d });
};
H.drawClear = (c, m) => {
  if (!requireRoom(c)) return;
  const mine = !!m.mine || c.room.roleOf(c.uid) !== "host";
  c.room.drawings = mine ? c.room.drawings.filter((d) => d.uid !== c.uid) : [];
  c.room.dirty = true;
  c.room.broadcast({ t: "drawSet", drawings: c.room.drawings });
};

H.nick = (c, m) => {
  c.nick = str(m.nick, 24);
  if (Array.isArray(m.color)) c.color = m.color.slice(0, 3).map((v) => Math.max(0, Math.min(255, num(v))));
  if (c.room) { c.room.names[c.uid] = c.display(); c.room.broadcast({ t: "player~", p: { pid: c.pid, nick: c.nick, color: c.color } }); }
};

// --- host tools
H.role = (c, m) => {
  if (!requireRole(c, "host")) return;
  const p = findPlayer(c.room, m.pid), role = ROLES.includes(m.role) ? m.role : null;
  if (!p || !role || p.uid === c.room.ownerUid) return;
  if (role === "host") return c.send({ t: "error", msg: "Use transfer to make someone host" });
  c.room.roles[p.uid] = role;
  c.room.dirty = true;
  c.room.broadcast({ t: "player~", p: { pid: p.pid, role } });
};
H.transfer = (c, m) => {
  if (!requireRoom(c) || c.room.ownerUid !== c.uid) return c.send({ t: "error", msg: "Only the owner can transfer host" });
  const p = c.room.players.get(Number(m.pid));
  if (!p) return;
  c.room.roles[c.uid] = "builder";
  c.room.ownerUid = p.uid;
  delete c.room.roles[p.uid];
  c.room.dirty = true;
  c.room.broadcast({ t: "player~", p: { pid: p.pid, role: "host" } });
  c.room.broadcast({ t: "player~", p: { pid: c.pid, role: "builder" } });
  c.room.broadcast({ t: "system", text: `${p.display()} is now the host` });
};
H.kick = (c, m) => {
  if (!requireRole(c, "host")) return;
  const p = findPlayer(c.room, m.pid);
  if (!p || p.uid === c.room.ownerUid) return;
  p.send({ t: "kicked", reason: m.ban ? "You were banned" : "You were kicked" });
  if (m.ban) c.room.bans.add(p.uid);
  leaveRoom(p, m.ban ? "banned" : "kicked");
  c.room.dirty = true;
};
H.unban = (c, m) => { if (requireRole(c, "host")) { c.room.bans.delete(str(m.uid, 64)); c.room.dirty = true; } };

H.room = (c, m) => {
  if (!requireRole(c, "host")) return;
  const r = c.room;
  if (typeof m.name === "string") r.name = str(m.name, 40) || r.name;
  if (typeof m.motd === "string") r.motd = str(m.motd, 300);
  if (typeof m.slowMs === "number") r.slowMs = Math.max(0, Math.min(60000, m.slowMs));
  if (typeof m.rate === "number") r.rate = Math.max(5, Math.min(5000, m.rate));
  if (typeof m.frozen === "boolean") r.frozen = m.frozen;
  if (typeof m.public === "boolean") r.public = m.public;
  if (typeof m.approval === "boolean") r.approval = m.approval;
  if (Array.isArray(m.tags)) r.tags = m.tags.slice(0, 5).map((t) => str(t, 16));
  if (ROLES.includes(m.defaultRole) && m.defaultRole !== "host") r.defaultRole = m.defaultRole;
  if (typeof m.password === "string") r.pw = hashPw(str(m.password, 64));
  if (typeof m.finished === "boolean") {
    if (m.finished && !r.finished) r.broadcast({ t: "confetti", by: c.display() });
    r.finished = m.finished;
  }
  r.dirty = true;
  r.broadcast({ t: "room~", room: r.settingsInfo() });
};

H.lock = (c, m) => {
  if (!requireRole(c, "builder")) return;
  const ids = Array.isArray(m.ids) ? m.ids.slice(0, 20000).map((s) => str(s, 40)) : [];
  const isHost = c.room.roleOf(c.uid) === "host";
  for (const id of ids) {
    if (m.on) c.room.locks.add(id);
    else if (isHost) c.room.locks.delete(id);
  }
  if (!m.on && !isHost) c.send({ t: "error", msg: "Only the host can unlock objects" });
  c.room.dirty = true;
  c.room.broadcast({ t: "locks", on: !!m.on && true, ids: m.on || isHost ? ids : [] , by: c.pid });
};

// --- build requests / tasks
H.task = (c, m) => {
  if (!requireRole(c, "decorator")) return;
  const r = c.room;
  if (m.op === "add") {
    const rect = Array.isArray(m.rect) ? m.rect.slice(0, 4).map((v) => num(v)) : [0, 0, 0, 0];
    r.tasks.push({ id: rid(8), text: str(m.text, 120) || "Needs work", rect, by: c.display(), claim: null, done: false, ts: now() });
  } else {
    const t = r.tasks.find((x) => x.id === m.id);
    if (!t) return;
    if (m.op === "claim") t.claim = t.claim === c.display() ? null : c.display();
    else if (m.op === "done") { t.done = !t.done; if (t.done) { r.statsOf(c.uid).tasksDone++; checkAchievements(r, c); } }
    else if (m.op === "del") r.tasks = r.tasks.filter((x) => x !== t);
  }
  r.dirty = true;
  r.broadcast({ t: "tasks", tasks: r.tasks });
};

// --- votes
H.vote = (c, m) => {
  if (!requireRoom(c)) return;
  const r = c.room;
  if (m.op === "new") {
    if (RANK[r.roleOf(c.uid)] < RANK.builder) return;
    r.votes.push({ id: rid(8), q: str(m.q, 120) || "Keep this part?", rect: Array.isArray(m.rect) ? m.rect.slice(0, 4).map((v) => num(v)) : null, by: c.display(), yes: [], no: [], open: true, ts: now() });
    if (r.votes.length > 50) r.votes.shift();
  } else {
    const v = r.votes.find((x) => x.id === m.id);
    if (!v) return;
    if (m.op === "cast" && v.open) {
      v.yes = v.yes.filter((u) => u !== c.uid); v.no = v.no.filter((u) => u !== c.uid);
      (m.v > 0 ? v.yes : v.no).push(c.uid);
    } else if (m.op === "close" && (r.roleOf(c.uid) === "host" || v.by === c.display())) v.open = false;
    else if (m.op === "del" && r.roleOf(c.uid) === "host") r.votes = r.votes.filter((x) => x !== v);
  }
  r.dirty = true;
  r.broadcast({ t: "votes", votes: r.votes.map((v) => ({ ...v, yes: v.yes.length, no: v.no.length })) });
};

// --- shared stopwatch
H.timer = (c, m) => {
  if (!requireRole(c, "host")) return;
  const tm = c.room.timer, t = now();
  if (m.op === "start" && !tm.running) { tm.running = true; tm.start = t; }
  else if (m.op === "stop" && tm.running) { tm.running = false; tm.acc += t - tm.start; }
  else if (m.op === "reset") { tm.running = false; tm.acc = 0; tm.start = 0; }
  c.room.dirty = true;
  c.room.broadcast({ t: "timer", timer: tm, now: t });
};

// --- group id manager: hands out group ids nobody is using
H.groups = (c, m) => {
  if (!requireRole(c, "builder")) return;
  const r = c.room, n = Math.max(1, Math.min(100, num(m.n, 1)));
  const used = new Set(Object.keys(r.groupClaims).map(Number));
  for (const o of r.objects.values()) for (const g of groupsOf(o.s)) used.add(g);
  const ids = [];
  for (let g = 1; g <= 9999 && ids.length < n; g++) if (!used.has(g)) ids.push(g);
  for (const g of ids) r.groupClaims[g] = c.uid;
  r.dirty = true;
  c.send({ t: "groupsAlloc", ids });
  r.broadcast({ t: "groupClaims", claims: Object.fromEntries(Object.entries(r.groupClaims).map(([g, u]) => [g, r.names[u] || "?"])) });
};
H.groupClaims = (c) => {
  if (!requireRoom(c)) return;
  const r = c.room;
  c.send({ t: "groupClaims", claims: Object.fromEntries(Object.entries(r.groupClaims).map(([g, u]) => [g, r.names[u] || "?"])) });
};

// --- template library
H.tpl = (c, m) => {
  if (!requireRoom(c)) return;
  const r = c.room;
  if (m.op === "save") {
    if (RANK[r.roleOf(c.uid)] < RANK.decorator) return;
    const s = str(m.s, 2_000_000);
    if (!s) return;
    r.templates.push({ id: rid(8), name: str(m.name, 40) || "Template", by: c.display(), count: num(m.count), s, ts: now() });
    if (r.templates.length > 100) r.templates.shift();
    r.dirty = true;
  } else if (m.op === "get") {
    const tp = r.templates.find((x) => x.id === m.id);
    if (tp) c.send({ t: "tplData", id: tp.id, name: tp.name, s: tp.s });
    return;
  } else if (m.op === "list") {
    return c.send({ t: "tpls", list: r.templates.map(({ id, name, by, count, ts }) => ({ id, name, by, count, ts })) });
  } else if (m.op === "del") {
    const tp = r.templates.find((x) => x.id === m.id);
    if (tp && (tp.by === c.display() || r.roleOf(c.uid) === "host")) { r.templates = r.templates.filter((x) => x !== tp); r.dirty = true; }
  }
  r.broadcast({ t: "tpls", list: r.templates.map(({ id, name, by, count, ts }) => ({ id, name, by, count, ts })) });
};

// --- timeline / history
H.history = (c, m) => {
  if (!requireRoom(c)) return;
  const r = c.room, limit = Math.max(1, Math.min(500, num(m.limit, 200)));
  const before = num(m.before, Infinity);
  const list = [];
  for (let i = r.log.length - 1; i >= 0 && list.length < limit; i--) {
    const e = r.log[i];
    if (e.q >= before) continue;
    let add = 0, del = 0, upd = 0;
    for (const o of e.ops) o.o === "add" ? add++ : o.o === "del" ? del++ : upd++;
    list.push({ q: e.q, t: e.t, name: r.names[e.a] || "?", add, del, upd, tag: e.tag || null });
  }
  c.send({ t: "history", list, oldest: r.log.length ? r.log[0].q : r.seq });
};

function commitInverse(c, entries, tag) {
  const r = c.room;
  const inv = r.inverseOf(entries);
  if (!inv.length) return c.send({ t: "error", msg: "Nothing to undo" });
  const { applied } = r.applyOps(c.uid, inv, { force: true, tag });
  r.broadcast({ t: "ops", by: 0, seq: r.seq, ops: applied.map(({ o, id, s, k }) => ({ o, id, s, k })) });
  r.broadcast({ t: "system", text: `${c.display()} ${tag} (${applied.length} changes)` });
}

H.restore = (c, m) => {
  if (!requireRole(c, "host")) return;
  const r = c.room, q = num(m.seq, -1);
  if (q < 0 || q >= r.seq) return;
  if (r.log.length && q < r.log[0].q - 1) return c.send({ t: "error", msg: "That point is too old to restore" });
  const entries = r.log.filter((e) => e.q > q).reverse();
  commitInverse(c, entries, `restored the level to #${q}`);
};

H.undoUser = (c, m) => {
  if (!requireRoom(c)) return;
  const r = c.room;
  const target = m.pid ? findPlayer(r, m.pid) : c;
  const tuid = target ? target.uid : str(m.uid, 64);
  if (tuid !== c.uid && r.roleOf(c.uid) !== "host") return c.send({ t: "error", msg: "Only the host can undo other players" });
  const n = Math.max(1, Math.min(1000, num(m.n, 1)));
  const entries = [];
  for (let i = r.log.length - 1; i >= 0 && entries.length < n; i--) if (r.log[i].a === tuid && !r.log[i].tag) entries.push(r.log[i]);
  // only undo ops that are still the latest state of their object
  const filtered = entries.map((e) => ({ ...e, ops: e.ops.filter((o) => {
    const cur = r.objects.get(o.id);
    return o.o === "del" ? !cur : cur && cur.s === o.s;
  }) }));
  commitInverse(c, filtered, `undid ${n} action(s) by ${r.names[tuid] || "someone"}`);
};

// session recording: full op log for timelapse playback
H.replay = (c, m) => {
  if (!requireRoom(c)) return;
  const r = c.room, from = num(m.from, 0), max = 2000;
  const out = [];
  for (const e of r.log) if (e.q > from) { out.push({ q: e.q, t: e.t, a: r.names[e.a] || "?", ops: e.ops.map(({ o, id, s }) => ({ o, id, s })) }); if (out.length >= max) break; }
  c.send({ t: "replay", list: out, done: out.length < max, first: r.log.length ? r.log[0].q : 0 });
};

H.leaderboard = (c) => {
  if (!requireRoom(c)) return;
  const r = c.room, counts = {};
  for (const o of r.objects.values()) counts[o.a] = (counts[o.a] || 0) + 1;
  const live = {};
  for (const p of r.players.values()) live[p.uid] = now() - p.joinedAt;
  const lb = Object.entries(r.stats).map(([uid, s]) => ({
    uid, name: r.names[uid] || "?", placed: s.placed, deleted: s.deleted, chats: s.chats,
    timeMs: s.timeMs + (live[uid] || 0), objectsNow: counts[uid] || 0, ach: (r.ach[uid] || []).length,
  })).sort((a, b) => b.objectsNow - a.objectsNow);
  c.send({ t: "leaderboard", list: lb });
};

// credits: who built what, for the auto-credits object and heatmap legend
H.credits = (c) => {
  if (!requireRoom(c)) return;
  const r = c.room, counts = {};
  for (const o of r.objects.values()) counts[o.a] = (counts[o.a] || 0) + 1;
  const list = Object.entries(counts).map(([uid, n]) => ({ uid, name: r.names[uid] || "?", n })).sort((a, b) => b.n - a.n);
  c.send({ t: "credits", list, host: r.names[r.ownerUid] || "?", total: r.objects.size });
};

H.lat = (c, m) => { c.send({ t: "lat", ts: m.ts }); if (typeof m.rtt === "number") c.lastPing = Math.round(m.rtt); };

H.resync = (c) => { if (requireRoom(c) && c.room.players.has(c.pid)) c.send({ t: "reset", ...c.room.snapshot() }); };

// ---------------------------------------------------------------- server
const server = http.createServer((req, res) => {
  if (req.url === "/rooms") {
    res.writeHead(200, { "content-type": "application/json", "access-control-allow-origin": "*" });
    return res.end(JSON.stringify([...rooms.values()].filter((r) => r.public).map((r) => r.info())));
  }
  res.writeHead(200, { "content-type": "text/plain" });
  let online = 0; for (const r of rooms.values()) online += r.players.size;
  res.end(`Multiplayer Editor server — made by x7wv\nrooms: ${rooms.size}\nonline: ${online}\n`);
});

const wss = new WebSocketServer({ server, maxPayload: 32 * 1024 * 1024, perMessageDeflate: { threshold: 1024 } });

wss.on("connection", (ws, req) => {
  const c = new Client(ws, req.headers["x-forwarded-for"] || req.socket.remoteAddress);
  ws.isAlive = true;
  ws.on("pong", () => (ws.isAlive = true));
  ws.on("message", (data) => {
    // basic flood guard: 300 messages / second
    const b = c.msgBucket, t = now();
    if (t - b.t > 1000) { b.t = t; b.n = 0; }
    if (++b.n > 300) return;
    let m;
    try { m = JSON.parse(data.toString()); } catch { return; }
    if (!m || typeof m.t !== "string" || !H[m.t]) return;
    if (m.t !== "lat" && m.t !== "play") {
      c.lastAct = t;
      if (c.afk && c.room) { c.afk = false; c.room.broadcast({ t: "player~", p: { pid: c.pid, afk: false } }); }
    }
    try { H[m.t](c, m); } catch (e) { console.error("handler", m.t, e); }
  });
  ws.on("close", () => leaveRoom(c, "disconnected"));
});

// heartbeats, AFK detection, activity feed, autosave, cleanup
setInterval(() => {
  for (const ws of wss.clients) { if (!ws.isAlive) { ws.terminate(); continue; } ws.isAlive = false; ws.ping(); }
}, 20000);

setInterval(() => {
  const t = now();
  for (const r of rooms.values()) {
    for (const c of r.players.values()) {
      if (!c.afk && t - c.lastAct > AFK_MS) { c.afk = true; r.broadcast({ t: "player~", p: { pid: c.pid, afk: true } }); }
    }
    // activity feed: "x7wv placed 240 objects"
    for (const [uid, fb] of Object.entries(r.feedBuf)) {
      const parts = [];
      if (fb.add) parts.push(`placed ${fb.add} object${fb.add > 1 ? "s" : ""}`);
      if (fb.del) parts.push(`deleted ${fb.del}`);
      if (fb.upd) parts.push(`edited ${fb.upd}`);
      if (parts.length) r.broadcast({ t: "feed", uid, name: r.names[uid] || "?", text: parts.join(", "), add: fb.add, del: fb.del, upd: fb.upd, ts: t });
    }
    r.feedBuf = {};
  }
}, 3000);

setInterval(() => {
  const t = now();
  for (const r of rooms.values()) {
    if (r.dirty) saveRoom(r);
    if (!r.players.size && t - r.lastActive > ROOM_TTL_MS) {
      rooms.delete(r.code);
      fs.unlink(path.join(DATA_DIR, r.code + ".json"), () => {});
    }
  }
}, 10000);

function shutdown() {
  console.log("saving rooms...");
  for (const r of rooms.values()) {
    for (const c of r.players.values()) r.statsOf(c.uid).timeMs += now() - c.joinedAt, (c.joinedAt = now());
    fs.writeFileSync(path.join(DATA_DIR, r.code + ".json"), JSON.stringify(r));
  }
  process.exit(0);
}
process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);

server.listen(PORT, () => console.log(`[Multiplayer Editor] made by x7wv — listening on :${PORT}`));
module.exports = { server };
