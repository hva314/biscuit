# HTTP Monitor

## What this is (in plain words)

HTTP Monitor turns the device into an always-on dashboard for one server on
your local network. It polls a URL on a timer, and whatever JSON the server
sends back — CPU load, disk usage, service status, anything you want — shows
up on screen. There's nothing to reflash: you change what the dashboard shows
by editing the script on your server, not by touching the device.

It does not compute or format anything itself. The server sends the exact
strings that appear on screen (`"6.0/16 GB"`, `"23%"`), and the device just
draws them. If you're writing the server side, see
[http-monitor-server-api.md](http-monitor-server-api.md) for the full
contract.

## Where to find it

**Apps → Tools → HTTP Monitor**

A small dot next to the entry lights up once `/biscuit/monitor.conf` exists on
the SD card — that's the "configured" indicator.

## The config file — `/biscuit/monitor.conf`

Put this file at the **top level of the `/biscuit` folder** on the SD card
(not in a subfolder). Format is one `key = value` pair per line:

- `#` starts a comment (the rest of the line is ignored).
- Blank lines are ignored.
- Whitespace around keys and values is trimmed.
- Unknown keys are ignored, so you can leave notes or disable a key by
  commenting it out.
- Both `key = value` and `key=value` (no spaces) are accepted.

| Key | Default if absent | Valid range | Notes |
|---|---|---|---|
| `url` | *(none — required)* | any HTTP/HTTPS URL | The only key that is fatal if missing. See "Error states" below. |
| `interval_sec` | 30 | 5–3600 | How often the device polls, in seconds. Out-of-range values are clamped. Also decides whether WiFi auto-drop is on — see "Power model" below. |
| `timeout_ms` | 5000 | 1000–30000 | How long the device waits for a response before giving up. Out-of-range values are clamped. |
| `full_refresh_every` | 20 | 0 disables, otherwise a positive count | Every this-many *redrawing* polls, the device does a deeper "clean" e-ink refresh. This is the visible full-screen flash. See "The refresh model" below. |
| `wifi_hold_sec` | 30 | 5–600 | Only relevant when auto-drop is on. How long WiFi stays associated after a directional button press, in seconds, so a burst of button presses doesn't reconnect on every single one. A further press before the hold expires re-arms it. |
| `battery_min_pct` | 5 | 0 disables, otherwise 0–50 | On battery (not USB), the device stops polling and shows a terminal "Battery critical" screen once the battery drops to this percentage or below. `0` disables the guard. See "Power model" below. |
| `auth_header` | *(none — no header sent)* | a full HTTP header, e.g. `Authorization: Bearer abc123` | Sent verbatim as an HTTP header on every request (including button-action requests). For access control, not for confidentiality — see the security note in the server API doc. |
| `font_size` | 2 | 0–3 | **Fallback** dashboard font size (0 smallest … 3 largest), used only when the server does not send a top-level `fontSize`. The server is the normal way to set this — see the server API doc. |
| `action_url` | *(none — buttons inert)* | a base URL, e.g. `http://192.168.0.24:8777/cmd` | Base URL for the four button actions. Left/Right/Up/Down each send `GET <action_url>/left`, `/right`, `/up`, `/down`. Absent (the default) means all four buttons do nothing. See "Buttons" below. |

The device has no title of its own — the header always shows whatever the
server's own `title` field last sent (see the server API doc), falling back to
the literal string `HTTP Monitor` before the first successful poll. The header
is one line, left to right: the title, the poll interval (`"30s"`), the
server's `updated` timestamp, and a small WiFi indicator in the far-right
corner — see "The header" below.

If the file is missing entirely, or `url` is missing or blank, the tool shows
an on-screen error naming the exact problem so you can fix it without a
cable — see "Error states" below.

### Example file

```
# biscuit HTTP Monitor
url                = http://192.168.1.10:8080/status.json
interval_sec       = 30      # 5..3600; > 300 turns on WiFi auto-drop
timeout_ms         = 5000    # 1000..30000
full_refresh_every = 20      # 0 disables
wifi_hold_sec      = 30      # 5..600; only relevant when auto-drop is on
battery_min_pct    = 5       # 0 disables; 0..50 otherwise
font_size          = 2       # 0..3, fallback only; the server normally sets this
# action_url       = http://192.168.1.10:8777/cmd
# auth_header      = Authorization: Bearer abc123
```

Copy this, edit `url` to point at your own server, and drop it at
`/biscuit/monitor.conf` on the SD card.

## Buttons

| Button | Action |
|---|---|
| Back | Exit the tool |
| Confirm | Force an immediate refresh (doesn't wait for the next scheduled poll) |
| Left / Right / Up / Down | Send a command to `action_url` (see below) |

The dashboard does not scroll and its font size is not adjustable on the
device — the server controls the font size and is responsible for sending an
amount of content that fits. Anything that doesn't fit is clipped at a row
boundary. See "Fitting the screen" in the server API doc.

### Button actions

If `action_url` is set, Left/Right/Up/Down each send a plain `GET` request to
`<action_url>/<slot>` — `left`, `right`, `up`, or `down` — and show a brief
"sent" (or failure) indicator just above the button hints. Nothing else
happens: the reply body is not rendered, the poll timer is not reset, and
there is no re-poll. This is a fire-and-forget remote control, not a
data-fetching action — use it for things like toggling lights or nudging
volume on the server. If `action_url` is absent (the default), all four
buttons are inert, same as before this feature existed.

A button always sends the same action regardless of `rotation` (see the
server API doc) — flipping the screen only changes what's drawn, not which
physical button means what.

## Power model

HTTP Monitor is meant to be a permanently-mounted wall dashboard, so it is
built to run unattended on battery for as long as possible rather than
requiring USB power. It suppresses the device's normal idle-sleep timeout
while the dashboard is showing (that's still the point of a dashboard: the
screen has to keep polling and updating), but it no longer needs full CPU
speed, an associated WiFi radio, or a fully-awake CPU to do that. Three
mechanisms combine to make that possible:

1. **CPU downclocking between polls.** While the dashboard is idle (nothing to
   fetch, no button pending), the device is free to drop to its low-power CPU
   clock, the same as it would if this tool weren't holding the device awake.
2. **WiFi auto-drop**, when `interval_sec` is greater than 300 seconds. WiFi is
   powered off entirely between polls and only brought back up — headlessly,
   with no on-screen network picker — long enough to fetch and, if
   `action_url` is set, to send a button action. A directional button press
   also brings WiFi back up and keeps it up for `wifi_hold_sec` afterward, so a
   burst of presses doesn't pay the reconnect cost on every single one. Below
   300 seconds, auto-drop stays off and WiFi stays associated continuously —
   reconnecting takes a few seconds, which would dominate a fast poll cadence.
   The WiFi indicator in the header's top-right corner shows which state
   applies at a glance (see "The header" below).
3. **Sliced light sleep**, only once WiFi is actually down (i.e. only for
   auto-drop configs — sleeping while still associated risks a missed-beacon
   disconnect). Between polls, the device sleeps in ~40ms slices, waking
   briefly to check for a button press (the four directional buttons are read
   from an analog ladder, not digital pins, so there is no hardware interrupt
   to wake on — the device has to wake up and poll for a press) and to see
   whether a poll or a WiFi hold is due. This is what lets the CPU spend the
   large majority of idle time genuinely asleep rather than merely
   downclocked, while a button press still feels responsive.

**Battery-critical guard.** If the device is running on battery power (no
USB connected) and the battery drops to `battery_min_pct` or below, the device stops
polling, drops WiFi, and shows a terminal "Battery critical" screen instead of
continuing to redraw the panel — a brownout partway through an e-ink update
can leave the panel visibly, permanently half-refreshed, which is worse than
just stopping. Plugging in USB resumes monitoring automatically; no restart
needed. Set `battery_min_pct = 0` to disable this guard.

**Honesty about the numbers:** the mechanisms above are the same techniques
that give battery-powered ESP32 devices multi-week to multi-month runtimes in
general, but this document does not have a measured milliamp-hour figure for
this specific tool to give you. Actual runtime depends on your `interval_sec`
(auto-drop only pays off above 300s), how much your server's data actually
changes (a static dashboard skips the e-ink update entirely — see "The refresh
model" below), and the device's own quiescent draw. Treat any number you hear
as an estimate, not a spec, until someone has actually measured it on the
hardware you're running.

## The header

The dashboard header is one line, left to right:

```
My Server              30s 14:03:22   ((o))
```

- **Title** (left, bold) — the server's `title` field, truncated to fit.
- **Poll interval** — the current `interval_sec`, shown as e.g. `"30s"`.
- **Updated** — the server's own `updated` field, drawn exactly as the server
  sent it. This is not a device clock and is not NTP-synced; it means whatever
  the server meant it to mean.
- **WiFi indicator**, far-right corner — a small vector glyph, not a bitmap,
  in one of three states:

  | State | Glyph | Meaning |
  |---|---|---|
  | Always on | filled dot + two signal arcs | Auto-drop is off (`interval_sec` ≤ 300) — WiFi stays associated continuously. |
  | Dropped | outline dot + arcs + diagonal slash | Auto-drop is on and WiFi is currently powered off, between polls. |
  | Held | filled dot + arcs + underscore bar | Auto-drop is on, but WiFi is being kept up for `wifi_hold_sec` after a button press. |

There is no server-IP display and no liveness dial (an earlier version of this
tool had one — a ticking clock hand in this same corner — but it cost a
full-panel e-ink update on every tick and existed only to prove the tool
hadn't frozen; the WiFi indicator and the `updated` timestamp already do that
job for free).

## The refresh model

This tool is designed to sit on screen for hours, so it tries hard to touch the
e-ink panel as little as possible. Every panel update costs power and contributes
to ghosting and long-term wear, so the rule is: **only redraw when the picture
actually changes.**

Two things can cause a redraw:

1. **A poll that returned different data.** The device compares each response
   against what is already on screen and does nothing at all if they match. A
   dashboard whose values are stable will leave the panel completely untouched,
   however often it polls.
2. **A manual refresh** (Confirm) or entering the tool.

With unchanging server data, the panel holds a completely static image
indefinitely — there is nothing left in this tool that redraws on a timer
rather than on an actual change.

Most redraws are fast updates. E-ink panels build up faint ghosting from these
over time — old pixel patterns linger faintly under new ones. Every
`full_refresh_every` redraws, the device does a deeper "clean" full refresh that
clears this ghosting, at the cost of a brief, more visible flash. Idle polls that
changed nothing do **not** count toward that cadence: a panel that has not been
touched has no ghosting to clean, and firing a full-screen flash at it would be
pure wear for no benefit. Setting `full_refresh_every` to `0` disables clean
refreshes entirely (not recommended for long-running displays).
