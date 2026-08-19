# HTTP Monitor — server implementer contract

This is the contract for the server side of biscuit's HTTP Monitor tool (see
[http-monitor.md](http-monitor.md) for the device-side user guide). It should
be everything you need to write a working endpoint without reading any
firmware source.

## 1. Overview

The device sends a plain `GET` request to the URL you configure in
`monitor.conf`, once every `interval_sec` seconds (default 30), and expects a
single JSON object back. It renders that object as a dashboard: a title, a
timestamp, and a list of sections. Each section contains typed rows — the
classic label/value pair with an optional progress bar, plus text, divider,
spacer, and glyph bands — with shared alignment, bold, and font-size
formatting (see §2).

The intended deployment is **plain HTTP over a trusted local network** — see
the security note at the end of this document before exposing the endpoint
any wider than that.

## 2. Schema

```json
{
  "title": "prod-1",
  "updated": "2026-08-16 01:20:03",
  "sections": [
    { "heading": "System", "rows": [
        { "label": "CPU",    "value": "23%",       "bar": 23 },
        { "label": "Memory", "value": "6.0/16 GB",
          "bar": { "value": 37, "segments": 8, "width": 0.8, "align": "left" } },
        { "type": "text", "text": "Maintenance window Sun 02:00", "align": "center" },
        { "type": "divider", "label": "Disks" } ] },
    { "heading": "Disks", "rows": [
        { "label": "/data",  "value": "88%", "bar": 88, "alert": true } ] }
  ],
  "alerts": ["disk /data at 88%"]
}
```

| Field | Type | Required | Meaning |
|---|---|---|---|
| `title` | string | optional | Header text. Overrides the `title` set in the device's `monitor.conf`, if present. |
| `updated` | string | optional | A display-ready timestamp string. The device shows it as-is; it does not parse or reformat it. |
| `fontSize` | integer 0–3 | optional | Dashboard-wide font size (0 smallest … 3 largest). **This is how you set the font size** — the device has no font-size buttons. Omit it (or send anything outside 0–3) and the device falls back to `font_size` in its `monitor.conf`. Individual rows can still override it with `size`. |
| `sections` | array of section objects | **required** (may be empty, but see failure semantics) | The body of the dashboard. |
| `sections[].heading` | string | optional | Section heading, drawn above its rows. Omit for an unheaded group of rows. |
| `sections[].rows` | array of row objects | **required** | The rows in this section. |
| `sections[].rows[].type` | string | optional | Row kind: `kv` (default), `text`, `bar`, `spacer`, `divider`, `glyphs`. Unknown values fall back to `kv`. |
| `sections[].rows[].label` | string | required for `kv`/`bar`/`divider`/`glyphs` | Left-hand text for `kv`/`bar` rows, a centered caption for `divider` rows, a left label for `glyphs` rows. |
| `sections[].rows[].value` | string | required for `kv`/`bar` | Right-hand text, e.g. `"23%"`. |
| `sections[].rows[].bar` | integer 0–100, or object | optional | Progress bar for the row. Integer form is full-width between label and value. Object form: `value` (0–100), `segments` (1–24 cells), `width` (0.1–1.0, fraction of the label→value span), `align` (`left`/`center`/`right`). |
| `sections[].rows[].text` | string | required for `text` | The text to display; wraps to at most 2 lines. |
| `sections[].rows[].glyphs` | array of strings | required for `glyphs` | Up to 32 one-character cells drawn as geometric shapes (see "Row types" below). |
| `sections[].rows[].align` | string | optional | `left` (default), `center`, or `right`. Row-wide alignment (bar-less kv value placement, text line alignment, divider/glyph placement). |
| `sections[].rows[].bold` | boolean | optional | `true` draws the row's text bold. Defaults to `false`. |
| `sections[].rows[].size` | integer 0–3 | optional | Font-size ladder index (0 smallest … 3 largest). Omit to inherit the dashboard-wide font size (the top-level `fontSize`, or `monitor.conf`'s `font_size` if you don't send one). |
| `sections[].rows[].alert` | boolean | optional | `true` visually emphasizes the row (bold + a marker). Defaults to `false`. |
| `sections[].rows[].height` | integer 2–60 | optional | `spacer` rows only: band height in px (default 10). |
| `sections[].rows[].inset` | integer ≥ 0 | optional | `divider` rows only: inset each end of the rule from the content edges (default 0). |
| `sections[].rows[].lineWidth` | integer 1–2 | optional | `divider` rows only: rule thickness in px (default 1). |
| `alerts` | array of strings | optional | A separate, top-level list of alert messages, shown outside the section rows. |

### Row types

`type` selects the row's rendering; the other fields mean different things per
type:

| `type` | What renders |
|---|---|
| `kv` (default) | `label` left, `value` right-aligned, optional `bar` spanning between them. |
| `bar` | Same layout as `kv`; a `bar` is expected. |
| `text` | `text`, wrapped to at most 2 lines. |
| `spacer` | Blank vertical space, `height` px. |
| `divider` | A horizontal rule, optionally with a centered `label` caption. |
| `glyphs` | `glyphs` cells (16×16 px each at 4 px gaps), optionally preceded by a `label`. |

Glyph characters: `#` filled square, `o` hollow square, `.` filled disk, `+`
plus sign, `x` cross, `!` filled triangle, `^` up-triangle outline, `v`
down-triangle outline, space = blank cell; any other character renders as a
small text glyph.

**The device does no computation.** Send display-ready strings — `"6.0/16 GB"`,
not a raw byte count; `"23%"`, not a raw float. Formatting a percentage or a
byte count on the device is wasted work on a battery-and-flash-constrained
microcontroller; the server has a full language runtime and should do it.

## 3. Hard limits

Anything beyond these limits is **silently dropped** by the device — no error
is raised, the excess is simply not rendered.

| Limit | Value |
|---|---|
| Response body | 8 KB |
| Sections | 6 |
| Rows per section | 12 |
| Label | 24 chars (ellipsized) |
| Value | 16 chars (ellipsized) |
| Text (`text` rows) | 64 chars (wrapped to 2 lines) |
| Glyphs (`glyphs` rows) | 32 chars |
| Bar segments | 1–24 cells |
| Bar width | 10–100% of the label→value span |
| Alerts | 4 |

Design your payload to fit inside these before worrying about screen layout.

## 4. Screen-fit guidance

This is the section to read carefully if you want the dashboard to look
intentional rather than cramped or half-empty. The device exists in two panel
sizes, and the UI itself is drawn in portrait orientation regardless of the
panel's physical (landscape) shape:

| | X4 | X3 |
|---|---|---|
| UI space (portrait) | 480 w × 800 h | 528 w × 792 h |

### Row-budget formula

For the default Classic theme, the vertical space actually available for
your rows is:

```
contentBand = pageHeight - topPadding(5) - headerHeight(45) - verticalSpacing(10) - buttonHintsHeight(40) - verticalSpacing(10)
```

Worked out for each panel:

| | X4 | X3 |
|---|---|---|
| `contentBand` | 800 − 5 − 45 − 10 − 40 − 10 = **690 px** | 792 − 5 − 45 − 10 − 40 − 10 = **682 px** |
| Rows at 35 px pitch | **19 rows** | **19 rows** |

That's with **no section headings** (bar rows share the 35 px pitch of plain
`kv` rows, so they don't change the count).

The 35 px pitch is the row font's line height (29 px) plus 6 px of padding.
Rows and section headings occupy one line each at that same pitch, so treat
the budget as **19 lines total**, headings included.

### Caveats

- **A bar that cannot fit beside its label and value is dropped, not moved
  elsewhere.** The bar spans the full gap between label and value. If wide
  label + value text squeezes that span below a 24 px readable floor, the
  device drops the bar and renders the row as plain label/value text. Bar rows
  always share the 35 px pitch of a plain `kv` row — they never get taller.
  Short labels and values keep the bar.
- **Section headings consume budget too** — each `heading` you set costs one
  of the 19 lines.
- **The user can select a different theme, and you do not control which.**
  The row pitch itself is font-derived and stays 35 px on every theme, but the
  header and padding differ, which moves the budget a little:

  | Theme | Lines (X4 / X3) |
  |---|---|
  | Classic, Military | 19 / 19 |
  | Noir, Radar, Lyra | 18 / 18 |

  **Design against 18 lines** and the dashboard is correct on every theme and
  both panels.
- **The X3 is wider, not taller** (528 px vs 480 px). A layout tuned to look
  full-width on X4 will simply have some horizontal slack on X3 — never the
  reverse. Designing for 480 px width is safe on both panels.

### Recommendation

**Target ≤ 12 rows plus ≤ 4 headings** — 16 lines, leaving two lines of margin
against the 18-line worst case. That fits on one screen on both panels, under
every theme. This exact budget is covered by a rendering test
(`render_httpmonitor_dense_x3_top`), which asserts that nothing is clipped.

> **The dashboard does not scroll.** There is no way for the user to reach
> content that doesn't fit — rows past the bottom of the screen are **silently
> dropped**, clipped at a row boundary so you never see a half-drawn row. Fitting
> the screen is entirely the server's job.
>
> The two levers are this row budget and the `fontSize` field. If you need more
> rows, send a smaller `fontSize`; if you need bigger text, send fewer rows. When
> content is dropped the device logs
> `[INF] HTTPMON Content does not fit: N of M entries drawn` over serial, which is
> the fastest way to confirm you are over budget.

Note that the budget depends on the `fontSize` you send — the numbers above assume
the default. A larger `fontSize` means taller rows and correspondingly fewer of
them.

## 5. Worked example

A complete, stdlib-only Python 3 script — no `pip install` needed — that
builds a status payload from system state and writes it out for a web server
to serve as a static file.

```python
#!/usr/bin/env python3
"""Generate status.json for the biscuit HTTP Monitor tool.

Reads CPU / memory / disk / load / uptime from /proc and statvfs, checks a
list of systemd units, and writes an atomic status.json for the device to
poll. Stdlib only -- no pip installs required.
"""
import json
import os
import subprocess
import time

OUTPUT_PATH = "/var/www/monitor/status.json"
TITLE = "prod-1"

# systemd units to report on
UNITS = ["nginx", "postgresql", "redis-server"]


def read_cpu_sample():
    with open("/proc/stat") as f:
        line = f.readline()
    # "cpu  user nice system idle iowait irq softirq steal guest guest_nice"
    parts = [int(x) for x in line.split()[1:]]
    idle = parts[3] + parts[4]  # idle + iowait
    total = sum(parts)
    return total, idle


def read_cpu_percent(sample_interval=0.2):
    total1, idle1 = read_cpu_sample()
    time.sleep(sample_interval)
    total2, idle2 = read_cpu_sample()
    total_delta = total2 - total1
    idle_delta = idle2 - idle1
    if total_delta <= 0:
        return 0.0
    return 100.0 * (total_delta - idle_delta) / total_delta


def read_memory():
    values = {}
    with open("/proc/meminfo") as f:
        for line in f:
            key, _, rest = line.partition(":")
            values[key] = int(rest.strip().split()[0])  # kB
    total_kb = values["MemTotal"]
    available_kb = values.get("MemAvailable", values["MemFree"])
    used_kb = total_kb - available_kb
    used_gb = used_kb / 1024 / 1024
    total_gb = total_kb / 1024 / 1024
    pct = round(100.0 * used_kb / total_kb) if total_kb else 0
    return f"{used_gb:.1f}/{total_gb:.0f} GB", pct


def read_disk(path):
    st = os.statvfs(path)
    total = st.f_blocks * st.f_frsize
    free = st.f_bavail * st.f_frsize
    used = total - free
    pct = round(100.0 * used / total) if total else 0
    return f"{pct}%", pct


def read_load():
    load1, load5, load15 = os.getloadavg()
    return f"{load1:.2f} {load5:.2f} {load15:.2f}"


def read_uptime():
    with open("/proc/uptime") as f:
        uptime_seconds = float(f.readline().split()[0])
    days, rem = divmod(int(uptime_seconds), 86400)
    hours, rem = divmod(rem, 3600)
    minutes, _ = divmod(rem, 60)
    if days:
        return f"{days}d {hours}h"
    if hours:
        return f"{hours}h {minutes}m"
    return f"{minutes}m"


def unit_is_active(unit):
    result = subprocess.run(
        ["systemctl", "is-active", unit],
        capture_output=True,
        text=True,
        timeout=5,
    )
    return result.stdout.strip() == "active"


def build_status():
    cpu_pct = round(read_cpu_percent())
    mem_value, mem_pct = read_memory()
    disk_value, disk_pct = read_disk("/")
    alerts = []

    system_rows = [
        {"label": "CPU", "value": f"{cpu_pct}%", "bar": cpu_pct},
        {"label": "Memory", "value": mem_value, "bar": mem_pct},
        {"label": "Load", "value": read_load()},
        {"label": "Uptime", "value": read_uptime()},
    ]

    disk_row = {"label": "/", "value": disk_value, "bar": disk_pct}
    if disk_pct >= 85:
        disk_row["alert"] = True
        alerts.append(f"disk / at {disk_value}")
    disk_rows = [disk_row]

    service_rows = []
    for unit in UNITS:
        try:
            active = unit_is_active(unit)
        except (subprocess.SubprocessError, OSError):
            active = False
        row = {"label": unit, "value": "up" if active else "down"}
        if not active:
            row["alert"] = True
            alerts.append(f"{unit} is down")
        service_rows.append(row)

    status = {
        "title": TITLE,
        "updated": time.strftime("%Y-%m-%d %H:%M:%S"),
        "sections": [
            {"heading": "System", "rows": system_rows},
            {"heading": "Disks", "rows": disk_rows},
            {"heading": "Services", "rows": service_rows},
        ],
    }
    if alerts:
        status["alerts"] = alerts
    return status


def write_atomic(path, data):
    """Write JSON to `path` without ever exposing a half-written file.

    The device polls this file over HTTP on its own timer, with no
    coordination with the writer. If we wrote directly to `path`, a poll
    landing mid-write would read a truncated/invalid JSON body. Writing to a
    temp file in the same directory and then os.replace()-ing it over the
    target is atomic on POSIX filesystems: any reader sees either the old
    complete file or the new complete file, never a partial one.
    """
    directory = os.path.dirname(path) or "."
    tmp_path = os.path.join(directory, f".{os.path.basename(path)}.tmp")
    with open(tmp_path, "w") as f:
        json.dump(data, f)
    os.replace(tmp_path, path)


def main():
    status = build_status()
    write_atomic(OUTPUT_PATH, status)


if __name__ == "__main__":
    main()
```

Ran locally, this produces (formatted for readability; the real output is a
single line of compact JSON):

```json
{
  "title": "prod-1",
  "updated": "2026-08-16 01:23:21",
  "sections": [
    {"heading": "System", "rows": [
        {"label": "CPU", "value": "1%", "bar": 1},
        {"label": "Memory", "value": "7.2/14 GB", "bar": 50},
        {"label": "Load", "value": "2.66 2.46 1.70"},
        {"label": "Uptime", "value": "4d 9h"}]},
    {"heading": "Disks", "rows": [
        {"label": "/", "value": "81%", "bar": 81}]},
    {"heading": "Services", "rows": [
        {"label": "nginx", "value": "down", "alert": true},
        {"label": "postgresql", "value": "up"},
        {"label": "redis-server", "value": "down", "alert": true}]}
  ],
  "alerts": ["nginx is down", "redis-server is down"]
}
```

### Running it on a timer

Two options: `cron`, or a systemd timer. If you already have cron running and
don't need sub-minute precision, a one-line crontab entry is simpler than the
two files below:

```
* * * * * /usr/bin/python3 /opt/monitor/monitor_status.py
```

(cron's minimum granularity is one minute; for a 15-second cadence you need
systemd, as below.)

`/etc/systemd/system/biscuit-monitor.service`:

```ini
[Unit]
Description=biscuit HTTP Monitor status generator

[Service]
Type=oneshot
ExecStart=/usr/bin/python3 /opt/monitor/monitor_status.py
```

`/etc/systemd/system/biscuit-monitor.timer`:

```ini
[Unit]
Description=Run biscuit-monitor every 15s

[Timer]
OnBootSec=15s
OnUnitActiveSec=15s
AccuracySec=1s

[Install]
WantedBy=timers.target
```

Enable with `systemctl enable --now biscuit-monitor.timer`.

### Serving the file

One line is enough — this is a static file, not a dynamic endpoint.

nginx:

```nginx
location /status.json { root /var/www/monitor; }
```

Caddy:

```
handle /status.json {
    root * /var/www/monitor
    file_server
}
```

## 6. Verifying with `curl` before touching the device

Confirm the endpoint works on its own before pointing the device at it:

```sh
curl -s http://192.168.1.10:8080/status.json | python3 -m json.tool
```

If that prints nicely formatted JSON, the endpoint is good. If `json.tool`
errors out, the response is malformed — fix it there; the device will show
the same "malformed JSON" error described below with no more detail.

Also worth checking directly:

```sh
curl -s -o /dev/null -w '%{http_code} %{size_download} bytes\n' http://192.168.1.10:8080/status.json
```

confirms the status code is `200` and the body size is under the 8 KB limit.

## 7. Failure semantics

What the device shows for each failure mode, so you can reason about your own
error paths:

The error screen shows a bold headline, the detail message beneath it, and
`Back` / `Retry` hints. These are the exact strings:

| Condition | Headline | Detail shown |
|---|---|---|
| Request times out, connection refused, DNS failure | `Connection failed` | `Connection failed` |
| Non-200 HTTP status | `HTTP <code>` | `HTTP <code>` — e.g. `HTTP 404`, `HTTP 500` |
| Malformed JSON | `HTTP 200` | `JSON parse failed: <reason>` (the parser's own reason, e.g. `InvalidInput`) |
| Body over 8 KB, or no `Content-Length` | `HTTP 200` | `Response too large or unknown length` |
| WiFi unavailable | `Connection failed` | `WiFi not connected` |
| Empty `sections` **and** empty `alerts` | *(not an error)* | The dashboard renders with the header and a centered `No data` |

Two things worth designing around:

- **A failed poll retries automatically** on the next scheduled tick
  (`interval_sec` later). The user can also force an immediate retry with
  Confirm. You do not need to do anything special to recover from a transient
  outage — but note the device will keep polling a broken endpoint
  indefinitely, so it is on you to make the endpoint cheap to serve.
- **No partial dashboard is ever shown.** On any failure the previous good
  dashboard is replaced by the error screen, so a user glancing at the device
  can never mistake stale data for current data.

**A note on `Content-Length`:** the device rejects a response whose length it
cannot determine up front. If your server uses chunked transfer encoding
without a `Content-Length` header, the device will refuse it with
`Response too large or unknown length`. Serving a static file through nginx
or Caddy sets the header for you; a hand-rolled HTTP handler may not.

## 8. Security note

The intended deployment is a **trusted local network over plain HTTP**. HTTPS
works as a transport, but the device does not validate TLS certificates, so
HTTPS on its own is not a substitute for network isolation — it stops passive
eavesdropping on the wire but not a server impersonating your endpoint.

If the endpoint needs to be reachable beyond the LAN, put both the device and
the server on a **WireGuard or Tailscale** network rather than exposing the
endpoint directly to the internet.

The optional `auth_header` config key (set on the device, in
`monitor.conf`) is sent verbatim as an HTTP header on every request. Treat it
as an **access-control** mechanism — a shared secret the server can check
before returning data — not as a **confidentiality** mechanism. Anyone who can
observe plain HTTP traffic on the network can read it.
