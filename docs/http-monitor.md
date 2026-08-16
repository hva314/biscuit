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
| `full_refresh_every` | 20 | 0 disables, otherwise a positive count | Every this-many polls, the device does a deeper "clean" e-ink refresh instead of a normal partial update. See "The refresh model" below. |
| `title` | `HTTP Monitor` | any short string | Shown in the header. Overridable per-response by the server's own `title` field. |
| `auth_header` | *(none — no header sent)* | a full HTTP header, e.g. `Authorization: Bearer abc123` | Sent verbatim as an HTTP header on every request. For access control, not for confidentiality — see the security note in the server API doc. |

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
title              = prod-1
# auth_header      = Authorization: Bearer abc123
```

Copy this, edit `url` to point at your own server, and drop it at
`/biscuit/monitor.conf` on the SD card.

## Buttons

| Button | Action |
|---|---|
| Back | Exit the tool |
| Confirm | Force an immediate refresh (doesn't wait for the next scheduled poll) |
| Up / Down | Scroll, when the dashboard has more rows than fit on screen |

## Power: this tool keeps the device awake

While HTTP Monitor is open, it suppresses the device's normal idle-sleep
timeout so the screen keeps polling and updating instead of the device going
to sleep. That's the point of a dashboard — but it means the device is not
saving power while this tool is open.

**Use USB power for this tool.** If you run it on battery, the battery will
drain continuously for as long as the tool stays open, since the device never
sleeps.

## The refresh model

The device polls your server every `interval_sec` seconds and redraws the
screen with whatever it gets back. Most of those redraws are quick "partial"
updates that only touch the parts of the screen that changed.

E-ink panels build up faint ghosting from partial updates over time — old
pixel patterns linger faintly under new ones. Every `full_refresh_every`
polls, the device does a deeper "clean" refresh that clears this ghosting,
at the cost of a brief, more visible flash. Setting `full_refresh_every` to
`0` disables these clean refreshes entirely (not recommended for long-running
displays).

## Error states

| Condition | What you'll see | What to check |
|---|---|---|
| No config file | On-screen message naming the exact path (`/biscuit/monitor.conf`) and the required key | Create the file at the top level of `/biscuit` on the SD card, with at least a `url` key |
| Config present but no `url` | On-screen message naming `url` as missing | Add a `url = ...` line to the config file |
| WiFi not connected | On-screen message that the device isn't on a network | Connect to WiFi first (Settings → WiFi), then reopen the tool |
| Connection timeout | On-screen timeout message | Check the server is reachable from the device's network, and that `url` and `timeout_ms` are correct; a slow server may need a larger `timeout_ms` |
| Non-200 HTTP status | The actual status code shown on screen (e.g. "404", "500") | Check the URL is correct and the server endpoint is actually serving the file at that path |
| Malformed JSON | On-screen parse-error message | Verify the server's response is valid JSON — see the server API doc's `curl` verification steps |
| Response too large | On-screen "too large" message | Your server response must fit the 8 KB limit — see the hard limits table in [http-monitor-server-api.md](http-monitor-server-api.md) |

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Dashboard never appears, stuck on "no config" | `monitor.conf` is missing, misnamed, or in the wrong folder | Confirm the exact path `/biscuit/monitor.conf` (top level of `/biscuit`, not a subfolder) |
| Dashboard shows a status code instead of data | Server is up but returning an error page or wrong path | Check `url` against what actually serves your JSON; test with `curl` from another machine on the same network |
| Dashboard is stale / not updating | `interval_sec` set too high, or server not updating its file | Press Confirm to force a refresh; check the server-side script/timer is actually running |
| Screen ghosting builds up over time | `full_refresh_every` set too high or to `0` | Lower `full_refresh_every` (default 20) so clean refreshes happen more often |
| Device drains battery fast with this tool open | Expected — the tool disables idle sleep while open | Run on USB power for long-term use |
| Some rows are missing or truncated | Server response exceeds the device's hard limits (sections, rows, label/value length) | Trim your server output to the limits in [http-monitor-server-api.md](http-monitor-server-api.md) |
| Can't see all the data at once | Dashboard has more rows than fit on one screen | Use Up/Down to scroll, or trim the server's row count to the screen-fit guidance in the server API doc |

## Installing it on an X3

The tool is part of the firmware, so getting it onto the device means flashing a
build that contains it. On the X3 that is **Settings → Update from SD card** —
and there is a size constraint worth knowing about before you build.

The space available for firmware on an X3 is fixed by Xteink's original
partition layout, which biscuit cannot change without USB access that most X3
units do not have. A build too large for that slot is rejected outright with
*"Image larger than the OTA slot"*, and there is no workaround short of a
smaller build.

Verified on hardware: a `slim` build carrying this tool at **6,501,280 B was
rejected**, while the same build with `-DOMIT_FONTS` at **3,729,888 B was
accepted**. If your update is refused, the image is the first thing to suspect.
See [x3-support.md](x3-support.md#caveats) for the details and how to shrink a
build.

> **Status:** this tool has been confirmed working on a physical Xteink X3 —
> polling a real endpoint and rendering the dashboard. Earlier revisions of this
> document described only what the rendering tests showed.
