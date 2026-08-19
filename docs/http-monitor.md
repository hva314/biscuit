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
| `interval_sec` | 30 | 5–3600 | How often the device polls, in seconds. Out-of-range values are clamped. |
| `timeout_ms` | 5000 | 1000–30000 | How long the device waits for a response before giving up. Out-of-range values are clamped. |
| `full_refresh_every` | 20 | 0 disables, otherwise a positive count | Every this-many *redrawing* polls, the device does a deeper "clean" e-ink refresh. This is the visible full-screen flash. See "The refresh model" below. |
| `dial_tick_sec` | 5 | 0 disables, otherwise 1–60 | How often the liveness dial's hand advances, in seconds. Every tick costs a full-screen e-ink update, so this directly controls panel wear — set it to `0` for the calmest possible display. See "The liveness dial" below. |
| `title` | `HTTP Monitor` | any short string | Shown in the header. Overridable per-response by the server's own `title` field. |
| `auth_header` | *(none — no header sent)* | a full HTTP header, e.g. `Authorization: Bearer abc123` | Sent verbatim as an HTTP header on every request. For access control, not for confidentiality — see the security note in the server API doc. |
| `font_size` | 2 | 0–3 | **Fallback** dashboard font size (0 smallest … 3 largest), used only when the server does not send a top-level `fontSize`. The server is the normal way to set this — see the server API doc. |

If the file is missing entirely, or `url` is missing or blank, the tool shows
an on-screen error naming the exact problem so you can fix it without a
cable — see "Error states" below.

### Example file

```
# biscuit HTTP Monitor
url                = http://192.168.1.10:8080/status.json
interval_sec       = 30      # 5..3600
timeout_ms         = 5000    # 1000..30000
full_refresh_every = 20      # 0 disables
dial_tick_sec      = 5       # 0 disables the liveness dial
title              = prod-1
font_size          = 2       # 0..3, fallback only; the server normally sets this
# auth_header      = Authorization: Bearer abc123
```

Copy this, edit `url` to point at your own server, and drop it at
`/biscuit/monitor.conf` on the SD card.

## Buttons

| Button | Action |
|---|---|
| Back | Exit the tool |
| Confirm | Force an immediate refresh (doesn't wait for the next scheduled poll) |
| Left / Right | *(nothing)* |
| Up / Down | *(nothing)* |

Back and Confirm are the only inputs. The dashboard does not scroll and its font
size is not adjustable on the device — the server controls the font size and is
responsible for sending an amount of content that fits. Anything that doesn't fit
is clipped at a row boundary. See "Fitting the screen" in the server API doc.

## Power: this tool keeps the device awake

While HTTP Monitor is open, it suppresses the device's normal idle-sleep
timeout so the screen keeps polling and updating instead of the device going
to sleep. That's the point of a dashboard — but it means the device is not
saving power while this tool is open.

**Use USB power for this tool.** If you run it on battery, the battery will
drain continuously for as long as the tool stays open, since the device never
sleeps.

## The refresh model

This tool is designed to sit on screen for hours, so it tries hard to touch the
e-ink panel as little as possible. Every panel update costs power and contributes
to ghosting and long-term wear, so the rule is: **only redraw when the picture
actually changes.**

Three things can cause a redraw:

1. **A poll that returned different data.** The device compares each response
   against what is already on screen and does nothing at all if they match. A
   dashboard whose values are stable will leave the panel completely untouched,
   however often it polls.
2. **The liveness dial ticking** — see below. This is the only thing that redraws
   on a timer rather than on a change, which is why it is configurable.
3. **A manual refresh** (Confirm) or entering the tool.

Most redraws are fast updates. E-ink panels build up faint ghosting from these
over time — old pixel patterns linger faintly under new ones. Every
`full_refresh_every` redraws, the device does a deeper "clean" full refresh that
clears this ghosting, at the cost of a brief, more visible flash. Idle polls that
changed nothing do **not** count toward that cadence: a panel that has not been
touched has no ghosting to clean, and firing a full-screen flash at it would be
pure wear for no benefit. Setting `full_refresh_every` to `0` disables clean
refreshes entirely (not recommended for long-running displays).

> **For the quietest possible display**, set `dial_tick_sec = 0` and leave
> `full_refresh_every` at its default. With unchanging server data the panel will
> then hold a completely static image indefinitely.

### The liveness dial

The dashboard header's top-right corner shows a small clock face — a ring with a
hand that advances every `dial_tick_sec` seconds (5 by default). It is a quick "is
the device still working" check at a glance: while the dashboard is on screen the
hand keeps moving, so a glance tells you the tool hasn't frozen. The hand is
driven by the device's own clock, so it moves even between polls; it is unrelated
to the server's `updated` timestamp, which is drawn just to its left.

**The dial is the main thing that wears the panel.** Each tick repaints the
screen, so the tick period is a direct multiplier on how much work the panel does:
at one tick per second — the old, non-configurable behaviour — that is over 86,000
updates a day for a decorative hand. At the default 5 seconds it is a fifth of
that, and `dial_tick_sec = 0` removes it entirely, leaving the `updated` timestamp
as the liveness cue instead.

The hand only moves while the dashboard is actually showing. In the fetching and
error states nothing about the screen changes, so there is nothing to animate.
