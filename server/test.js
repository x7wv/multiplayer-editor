// Simulated-player test for the Multiplayer Editor server. Run: npm test
"use strict";
process.env.PORT = process.env.PORT || "18080";
process.env.DATA_DIR = require("path").join(require("os").tmpdir(), "me-test-" + Date.now());
const { server } = require("./server");
const WebSocket = require("ws");
const assert = require("assert");

function bot(name) {
  const ws = new WebSocket("ws://127.0.0.1:" + process.env.PORT);
  const inbox = [];
  const waiters = [];
  ws.on("message", (d) => {
    const m = JSON.parse(d.toString());
    inbox.push(m);
    for (const w of [...waiters]) if (w.pred(m)) { waiters.splice(waiters.indexOf(w), 1); w.res(m); }
  });
  const b = {
    ws, inbox,
    send: (m) => ws.send(JSON.stringify(m)),
    wait: (t, pred = () => true, ms = 3000) => {
      const hit = inbox.find((m) => m.t === t && pred(m) && !m._used);
      if (hit) { hit._used = true; return Promise.resolve(hit); }
      return new Promise((res, rej) => {
        const w = { pred: (m) => m.t === t && pred(m), res: (m) => { m._used = true; clearTimeout(to); res(m); } };
        const to = setTimeout(() => rej(new Error(`${name} timed out waiting for ${t}`)), ms);
        waiters.push(w);
      });
    },
    open: () => new Promise((r) => ws.on("open", r)),
  };
  return b;
}

(async () => {
  const host = bot("host"), a = bot("alice"), v = bot("viewer");
  await Promise.all([host.open(), a.open(), v.open()]);
  host.send({ t: "hello", uid: "u-host", name: "x7wv" }); await host.wait("welcome");
  a.send({ t: "hello", uid: "u-alice", name: "Alice" }); await a.wait("welcome");
  v.send({ t: "hello", uid: "u-view", name: "Vic" }); await v.wait("welcome");

  host.send({ t: "create", name: "Mega", password: "pw", public: true, tags: ["megacollab"], motd: "Welcome!", objects: [["h:1", "1,1,2,15,3,15", "g"]], settings: "kS38,1" });
  const j = await host.wait("joined");
  assert.strictEqual(j.role, "host"); assert.strictEqual(j.objects.length, 1);
  const code = j.code;

  a.send({ t: "list" }); const rl = await a.wait("rooms"); assert(rl.list.some((r) => r.code === code));
  a.send({ t: "join", code, password: "bad" }); await a.wait("error", (m) => /Wrong password/.test(m.msg));
  a.send({ t: "join", code, password: "pw" }); const ja = await a.wait("joined");
  assert.strictEqual(ja.objects.length, 1); assert.strictEqual(ja.room.motd, "Welcome!");
  await host.wait("player+");

  // sync
  a.send({ t: "ops", n: 1, ops: [{ o: "add", id: "a:1", s: "1,8,2,45,3,15", k: "g" }] });
  const op = await host.wait("ops"); assert.strictEqual(op.ops[0].id, "a:1");
  await a.wait("ack");
  a.send({ t: "ops", ops: [{ o: "upd", id: "h:1", s: "1,1,2,30,3,15,57,3.4", k: "g" }] }); await host.wait("ops");

  // viewer role
  v.send({ t: "join", code, password: "pw" }); const jv = await v.wait("joined");
  host.send({ t: "role", pid: jv.you, role: "viewer" }); await v.wait("player~", (m) => m.p.role === "viewer");
  v.send({ t: "ops", ops: [{ o: "add", id: "v:1", s: "1,1", k: "g" }] });
  const rej = await v.wait("reject"); assert.strictEqual(rej.fix[0].s, null);

  // decorator can't touch gameplay
  host.send({ t: "role", pid: ja.you, role: "decorator" }); await a.wait("player~", (m) => m.p.role === "decorator");
  a.send({ t: "ops", ops: [{ o: "del", id: "h:1" }] }); await a.wait("reject");
  a.send({ t: "ops", ops: [{ o: "add", id: "a:2", s: "1,211,2,0,3,0", k: "d" }] }); await host.wait("ops", (m) => m.ops[0].id === "a:2");
  host.send({ t: "role", pid: ja.you, role: "builder" }); await a.wait("player~", (m) => m.p.role === "builder");

  // locks
  host.send({ t: "lock", ids: ["h:1"], on: true }); await a.wait("locks");
  a.send({ t: "ops", ops: [{ o: "del", id: "h:1" }] }); await a.wait("reject");

  // freeze
  host.send({ t: "room", frozen: true }); await a.wait("room~", (m) => m.room.frozen);
  a.send({ t: "ops", ops: [{ o: "add", id: "a:3", s: "1,1", k: "g" }] }); await a.wait("reject", (m) => /frozen/.test(m.reason));
  host.send({ t: "room", frozen: false, slowMs: 5000 }); await a.wait("room~", (m) => !m.room.frozen);

  // chat + slow mode
  a.send({ t: "chat", text: "hi" }); await host.wait("chat", (m) => m.text === "hi");
  a.send({ t: "chat", text: "spam" }); await a.wait("error", (m) => /Slow mode/.test(m.msg));
  host.send({ t: "room", slowMs: 0 });

  // group id manager — 3 and 4 used by h:1
  a.send({ t: "groups", n: 3 }); const g = await a.wait("groupsAlloc"); assert.deepStrictEqual(g.ids, [1, 2, 5]);

  // tasks, votes, timer, pings, react, draw, templates
  a.send({ t: "task", op: "add", text: "needs deco", rect: [0, 0, 100, 100] }); const tk = await host.wait("tasks");
  host.send({ t: "task", op: "claim", id: tk.tasks[0].id }); await a.wait("tasks", (m) => m.tasks[0].claim === "x7wv");
  host.send({ t: "vote", op: "new", q: "keep?" }); const vt = await a.wait("votes");
  a.send({ t: "vote", op: "cast", id: vt.votes[0].id, v: 1 }); await host.wait("votes", (m) => m.votes[0].yes === 1);
  host.send({ t: "timer", op: "start" }); await a.wait("timer", (m) => m.timer.running);
  a.send({ t: "ping", x: 10, y: 20, label: "look here!" }); await host.wait("ping");
  a.send({ t: "react", e: "fire", x: 1, y: 2 }); await host.wait("react");
  a.send({ t: "draw", pts: [0, 0, 10, 10, 20, 5] }); await host.wait("draw");
  a.send({ t: "tpl", op: "save", name: "spike row", s: "1,8;1,8", count: 2 }); const tp = await host.wait("tpls");
  host.send({ t: "tpl", op: "get", id: tp.list[0].id }); await host.wait("tplData");

  // per-player undo: alice placed a:1 and a:2
  host.send({ t: "undoUser", pid: ja.you, n: 5 });
  const und = await a.wait("ops", (m) => m.by === 0);
  assert(und.ops.some((o) => o.o === "del" && o.id === "a:1"));

  // timeline restore to seq 0 → back to the original level
  host.send({ t: "history" }); const hist = await host.wait("history"); assert(hist.list.length >= 3);
  host.send({ t: "restore", seq: 0 }); await a.wait("ops", (m) => m.by === 0);
  host.send({ t: "resync" }); const rs = await host.wait("reset");
  assert.strictEqual(rs.objects.length, 1); assert.strictEqual(rs.objects[0][1], "1,1,2,15,3,15");

  // replay, leaderboard, credits, latency
  host.send({ t: "replay" }); const rp = await host.wait("replay"); assert(rp.list.length > 0);
  host.send({ t: "leaderboard" }); await host.wait("leaderboard");
  host.send({ t: "credits" }); const cr = await host.wait("credits"); assert.strictEqual(cr.host, "x7wv");
  a.send({ t: "lat", ts: 123 }); assert.strictEqual((await a.wait("lat")).ts, 123);
  await host.wait("feed");

  // approval + kick/ban
  host.send({ t: "room", approval: true });
  const late = bot("late"); await late.open();
  late.send({ t: "hello", uid: "u-late", name: "Late" }); await late.wait("welcome");
  late.send({ t: "join", code, password: "pw" }); await late.wait("pending");
  const req = await host.wait("joinRequest"); host.send({ t: "approve", pid: req.pid, ok: true }); await late.wait("joined");
  host.send({ t: "kick", pid: req.pid, ban: true }); await late.wait("kicked");
  late.send({ t: "join", code, password: "pw" }); await late.wait("error", (m) => /banned/.test(m.msg));

  // finished → confetti, transfer host
  host.send({ t: "room", finished: true }); await a.wait("confetti");
  host.send({ t: "transfer", pid: ja.you }); await a.wait("player~", (m) => m.p.pid === ja.you && m.p.role === "host");

  // rate limiter
  host.send({ t: "room", rate: 10 }); // host is now builder — should be rejected
  await host.wait("error");
  a.send({ t: "room", rate: 10 }); await host.wait("room~", (m) => m.room.rate === 10);
  host.send({ t: "ops", ops: Array.from({ length: 20 }, (_, i) => ({ o: "add", id: "spam:" + i, s: "1,1", k: "g" })) });
  await host.wait("reject", (m) => /limit/.test(m.reason) && m.fix.length === 10);

  console.log("✅ all server tests passed");
  for (const b of [host, a, v, late]) b.ws.close();
  server.close(); process.exit(0);
})().catch((e) => { console.error("❌", e); process.exit(1); });
