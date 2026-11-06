# Multiplayer Editor — made by x7wv

A free Geode mod for Geometry Dash 2.2081 that lets people build one level together, live.

```
mod/      the Geode mod (C++)  -> builds to x7wv.multiplayer-editor.geode
server/   the relay server (Node.js) everyone connects to
```

---

## 1. Build the mod (.geode file)

You don't need to install anything. GitHub builds it for you on all platforms.

1. Make a new GitHub repo (it can be private) and this repo already has it
   (the `.github` folder too, it's hidden on some computers).
2. Open the repo's **Actions** tab. A build named **Build Multiplayer Editor** starts by itself.
3. When it finishes (~10 min), click it and download **Build Output**. Inside is
   `x7wv.multiplayer-editor.geode`, a single file that works on Windows, Mac, Android and iPhone.
4. Install it: drop the `.geode` file into your Geode `mods` folder (or on mobile, use
   Geode → Mods → "Install from file").

If a build goes red, open it, copy the error, and send it to Claude to fix.

---

## 2. Host the server (free)

Everyone in a room must use the **same server**. Only one person (you) needs to run it.

### Option A: Oracle Cloud Always Free (best, runs 24/7)
Oracle's Always Free tier gives you an Arm VM (2 CPU / 12 GB RAM). That's far more than this
server needs, enough for hundreds of players. Sign-up asks for a card, but only to verify you.
If it says "Out of capacity", try another availability domain or try again later.

1. Sign up at oracle.com/cloud/free and create an instance: **Ubuntu**, shape **VM.Standard.A1.Flex**.
   Save the SSH key it gives you.
2. In the instance's **Virtual Cloud Network → Security List**, add an **Ingress rule**:
   source `0.0.0.0/0`, TCP, destination port **8080**.
3. SSH in and run:
   ```bash
   sudo apt update && sudo apt install -y nodejs npm
   sudo iptables -I INPUT -p tcp --dport 8080 -j ACCEPT && sudo netfilter-persistent save
   sudo mkdir -p /opt/multiplayer-editor && sudo chown ubuntu /opt/multiplayer-editor
   # upload server.js, package.json, package-lock.json and multiplayer-editor.service here, then:
   cd /opt/multiplayer-editor && npm ci --omit=dev
   sudo cp multiplayer-editor.service /etc/systemd/system/
   sudo systemctl enable --now multiplayer-editor
   ```
4. Open `http://YOUR-IP:8080` in a browser. If it says "Multiplayer Editor server — made by x7wv", it's working.
5. In GD: Geode → Multiplayer Editor → settings → **Server URL** = `ws://YOUR-IP:8080`.
   Everyone who joins your rooms uses the same URL.

### Option B: Your own PC (quick testing)
```bash
cd server
npm install
npm start
```
On the same Wi-Fi, people use `ws://YOUR-PC-LAN-IP:8080`. For friends elsewhere, forward port
8080 on your router, or use a free TCP tunnel like playit.gg. The server only runs while your PC is on.

Rooms are saved to `server/data/` and survive restarts. Empty rooms are deleted after 7 days.

To check the server works: `npm test` runs fake players through every feature.

---

## 3. Use it

- **Host:** open your level in the editor → tap **MP** (top of the screen) → **Host This Level**.
  Share the 6-letter code (or tap **Copy Invite**).
- **Join:** main menu → **MP** button (bottom row) → **Join by Code** or **Browse Rooms**.
  Joining opens the room in a new local level, so you keep a copy.
- **Tools** (button at the top of the editor): Timeline, Tasks, Votes, Templates, Group IDs,
  Leaderboard, Timelapse, Credits Text, Heatmap, Draw mode, Undo, Stopwatch and Reactions.

---

## Checklist status

✅ = done and in the mod

| Feature | Status |
|---|---|
| Instant sync: place / delete / move / rotate / scale / paste / edit | ✅ |
| Unique object IDs (no clashes or duplicates) | ✅ |
| Level settings sync (mode, speed, BG, ground, font, song, guidelines) | ✅ |
| Live named cursors | ✅ |
| Host / Join / Room codes / passwords | ✅ |
| Player list: kick, ban, transfer host | ✅ |
| Auto-reconnect + auto-saved rooms on the server | ✅ |
| "Made by x7wv" credits (popup, overlay, mod page, first launch) | ✅ |
| Text chat + typing indicator | ✅ |
| Emoji reactions | ✅ |
| Ghost camera (follow / jump to teammate) | ✅ |
| Roles: Host / Builder / Decorator / Viewer | ✅ |
| Activity feed ("x7wv placed 240 objects") | ✅ |
| Per-player undo | ✅ |
| Live playtest spectating | ✅ |
| Timeline / time-travel restore | ✅ |
| Auto collab-credits text object | ✅ |
| Custom themes incl. x7wv theme | ✅ |
| Heatmap overlay | ✅ |
| Pings | ✅ |
| Selection outlines | ✅ |
| Build requests / tasks | ✅ |
| Shared stopwatch | ✅ |
| Live object counter per player | ✅ (in player list) |
| Lock objects | ✅ |
| Shared group-ID manager | ✅ |
| Template library | ✅ |
| Draw mode | ✅ |
| Player profiles (icon, colors, name) | ✅ |
| Voting / polls | ✅ |
| Room MOTD | ✅ |
| Achievements | ✅ |
| Leaderboard | ✅ |
| AFK detection (dims idle players) | ✅ |
| Sound effects | ✅ |
| Confetti when level is finished | ✅ |
| Nicknames (`/nick name` in chat) and name colors | ✅ |
| Room browser with tags | ✅ |
| Invite codes for Discord | ✅ |
| Join requests (host approves) | ✅ |
| Chat slow mode | ✅ |
| Edit rate limiter (anti-grief) | ✅ |
| Freeze room | ✅ |
| Session recording / timelapse | ✅ |
| Ping / latency display | ✅ |
| Bandwidth saver (batched sync) | ✅ |
| Offline mode (edits merge back on reconnect) | ✅ |

### Known limits in v1.0
- Connections use plain `ws://` (not encrypted). Room passwords keep strangers out, but don't
  reuse an important password. Encrypted `wss://` is planned.
