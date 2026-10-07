# Tech Terminal Integration Roadmap

## Goal

Replace the SenseCAP stock menu flow with one Tech Terminal interface for the
480×480 touchscreen. Home, module pages, settings, and Wi-Fi all use the same
navigation shell. Modules exchange data through shared services instead of
owning separate network code.

## Current baseline

- ESP-IDF 5.4.4 / ESP32-S3, 8 MB PSRAM, capacitive touch.
- The dashboard and return-to-home Wi-Fi navigation are flashed and booting.
- The vendored UI library is LVGL 8.3.1. Keep it for this integration unless moving to LVGL 9 materially reduces implementation effort; the UI architecture does not depend on that migration.
- The generated SquareLine screens are LVGL 8 stock firmware screens. Wi-Fi
  scan and connection behavior currently live in `main/view/indicator_view.c`
  and `main/model/indicator_wifi.c`.
- ESP-IDF HTTP client support and a shared event loop are already in the build.
  Wi-Fi credentials and Tech Terminal preferences use separate NVS namespaces;
  the Tech Terminal settings loader supplies defaults for missing or invalid data.
- The Tech Terminal shell has five module cards, a Network route into the existing
  Wi-Fi scan/connect view, and versioned NVS settings. Settings now includes
  brightness and inactivity timeout controls, alongside browser, GitHub, Pi,
  and location preferences. A bounded shared HTTP GET helper is in `main/tech`;
  the Network Monitor shows live station details, and the Pi Monitor fetches and
  displays live status from its configured endpoint. Other module screens still
  need live data integration.

## Architecture to build

```
LVGL app shell and router
  ├── Home / Modules / Settings
  ├── Web Reader
  ├── Network Monitor ── Wi-Fi + device registry + health checks
  ├── Pi Monitor ─────── configured Pi status endpoints
  ├── GitHub ─────────── GitHub API client
  ├── F1 ─────────────── motorsport data provider
  └── Information ───── optional weather / news / space sources
          │
          ├── shared HTTP(S), DNS, time and cache services
          └── settings repository (NVS)
```

Each module gets its own UI, controller, and data model. Long scans and HTTP
requests run in FreeRTOS workers. Workers post results to the app event loop;
LVGL objects are updated only while holding the LVGL port mutex. Views never
perform blocking network calls.
## Work phases

### 0. Stabilize the base on LVGL 8.3.1

1. Preserve the working firmware and isolate the Tech Terminal UI from the
   generated stock UI.
2. Keep the current ESP-IDF display, touch, flush, and direct-mode integration.
   Consider LVGL 9 only if a concrete compatibility or maintenance need makes
   migration simpler than continuing on LVGL 8.
3. Retire generated stock screens from startup and navigation. Rebuild the
   needed Wi-Fi scan, connect, and password flows in the Tech Terminal visual
   system while reusing working radio and credential code.
4. Build and flash reviewable shell and module milestones.

**Done when:** startup and every reachable route use the Tech Terminal UI,
display and touch work reliably, and no stock menu is reachable.

### 1. App shell, routes, and settings foundation

- Create a central route table and a navigation stack. Home → module → detail
  pushes a route; Back pops to its parent. Home always returns to the dashboard.
- Add a persistent top bar with Back, Tech Terminal title, Wi-Fi/time status,
  and Settings. Use common page spacing, colors, loading, empty, error, and
  stale-data states.
- Add a Settings page and versioned preference model backed by NVS. Fix the
  current storage initializer to return errors and make reads handle missing
  keys with defaults.
- Add settings groups for display, time, Wi-Fi, refresh behavior, browser,
  devices, GitHub, and F1. Secrets are never logged or displayed as plain text.

**Done when:** touch navigation works in every direction; Home and Back never
enter stock screens; settings survive restart and invalid/missing stored values
fall back safely.
### 2. Shared network services and Network Monitor

- Extract Wi-Fi state and scan results from the stock view into a service/API
  consumed by a new Network Monitor screen.
- Show connection state, SSID, RSSI, IP address, DNS/gateway, internet reachability,
  last scan time, and a manual rescan action.
- Sort visible networks by signal and show security. Deduplicate identical SSIDs
  while retaining the strongest access point and reporting when several access
  points share that SSID. Allow a manual SSID for hidden networks.
- Audit scan capacity and channel coverage. The ESP32-S3 radio is 2.4 GHz only;
  5 GHz-only access points cannot be listed by this board.
- Add a configurable device registry (name, address, enabled, check interval)
  stored as versioned settings, not embedded in the UI. Show per-device online
  state and latency. Reuse the existing ping/network checks where practical.
- Use a shared HTTP(S) client from worker tasks with DNS resolution, timeouts,
  bounded responses, TLS certificate verification, status codes, and cache
  timestamps. The first implementation, `main/tech/tech_http.c`, provides a
  10-second GET, certificate-bundle TLS, 8 KB body limit, and up to three
  redirects. Module workers still need to adopt it.

**Done when:** Wi-Fi scan/connect/disconnect and configured device health checks
work from Tech Terminal pages, refresh cleanly, and show useful offline/errors.

### 3. Raspberry Pi Monitor

- Define a small companion agent for Linux Pis with JSON endpoints:
  `GET /api/v1/status` (hostname, CPU, memory, disk, temperature, uptime) and
  `GET /api/v1/services` (configured service names and states).
- Keep the agent lightweight and read-only. Document install, update, and
  endpoint configuration; support a per-device token if enabled.
- Configure one or more Pi hosts using the Network device registry. Poll in a
  worker, enforce timeouts, and keep the last good result with its age.
- Add a device overview, Pi detail page, and Services page using shared route
  and card components.

**Done when:** one Pi can be added without a firmware edit; online/offline,
metrics, service state, and stale data are distinguishable on screen.

**Implemented so far:** Settings accepts a Pi status URL. The Pi Monitor requests
`/api/v1/status` asynchronously through the shared bounded HTTP client and shows
hostname, CPU, memory, disk, temperature, and uptime. It reports request, HTTP,
JSON, and missing-metric errors. Service listing, cache age, and a documented
companion agent remain to be added.
### 4. Web Reader

- Build URL entry, load/stop, refresh, Back/Forward, readable page, and error
  states. Add a compact touch keyboard suited to 480×480.
- Validate and normalize HTTP/HTTPS URLs. Fetch asynchronously through the
  shared HTTP(S) service with verified TLS, timeouts, and a strict body limit.
- Parse a safe HTML subset into headings, paragraphs, lists, and links. Skip
  scripts, styles, and unsupported content; this is a text reader, not Chrome.
- Render extracted content in LVGL. Tapping a link navigates within the reader;
  retain a small in-memory history. Persist a home page and bookmarks in NVS.
- Use module handoff events so GitHub's “Open in Web” opens the same reader.

**Done when:** documentation, simple sites, and API pages load; links and
history work; bad URLs, TLS errors, oversize pages, and offline states are clear.

**Implemented so far:** URL entry, asynchronous bounded HTTP(S) fetch, basic
HTML text extraction with script/style removal, readable scrolling text, and
Back/Forward history are available. Link extraction and navigation, robust
parser handling, and saved bookmarks remain.

### 5. GitHub

- Add a GitHub API client on the shared HTTP service. Start with configurable
  public owner/repository lists, then add optional authenticated access.
- Display repository name, description, stars, open issues, and last update;
  add detail view for recent commits and issue/pull-request counts.
- Cache responses and show their age. Respect API rate-limit headers, paginate
  deliberately, and provide refresh/error states.
- Add owner/repository preferences to Settings. If a token is required, enter
  and store it through a masked settings flow; never log it.
- Add “Open in Web” handoff to the Reader for repository pages and links.

**Done when:** configured public repositories load without a token; optional
private access works when configured; the UI remains useful during API limits
or temporary network loss.

**Implemented so far:** the first configured public repository loads asynchronously
through the shared HTTP service. The screen shows name, description, stars, open
issues, and update timestamp, with refresh/error states and an Open in Web handoff.
Repository lists, API cache/rate-limit detail, commits, and private-token support
remain.
### 6. F1

- Define a provider-neutral data interface before selecting a live timing source.
  Verify provider availability, terms, and endpoints when implementation starts.
- Build schedule, next-race countdown, session results, drivers, and teams in
  stages. Use UTC timestamps and the device's configured time zone.
- Keep API parsing separate from the screen, cache the last successful response,
  label its age, and avoid frequent polling. User preferences include favorite
  driver/team and refresh interval.

**Done when:** schedule and results pages use real provider data, recover from
offline/rate-limited responses, and count down using synchronized time.

### 7. Information (after the five diagram modules)

The longer design you pasted adds Information, while the architecture diagram
lists five modules. Keep Information as an optional sixth module. Start with one
source (for example weather or space status), then add other sources through a
small provider interface. Keep city/source selection and refresh intervals in
Settings. Show source and update age.

### 8. Finish integration and polish

- Remove remaining stock-screen references and generated assets that are no
  longer used. Keep hardware drivers and working BSP code separate from UI.
- Review memory use, screen lifecycle, response limits, touch target sizes,
  long SSIDs/URLs, keyboard behavior, and repeated navigation.
- Add startup, Wi-Fi, offline, loading, and error behavior across modules.
- Build on ESP-IDF 5.4.4 for ESP32-S3 after each milestone. Flash COM6 only for
  reviewable UI/firmware milestones, then confirm serial boot and touch flow.

## Recommended implementation order

1. Finish the LVGL 8.3.1 app shell/router and versioned NVS settings.
2. Add shared asynchronous HTTP(S) service and Network Monitor.
3. Add Pi Monitor and document its companion endpoint.
4. Add Web Reader and common HTML text extraction.
5. Add GitHub API and Reader handoff.
6. Add F1 provider and screens.
7. Add optional Information providers and final polish.
8. Reconsider LVGL 9 only if it materially simplifies remaining work.

Each phase should leave a buildable firmware. Network modules should first use
mock/cache data where needed, then switch to live providers without redesigning
their screens. External choices still to settle at implementation time are Pi
addresses/agent deployment, GitHub owner and private-repo token needs, F1 data
provider, and desired Information sources/location.
