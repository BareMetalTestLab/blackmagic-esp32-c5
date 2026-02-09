# blackmagic-esp32-c5

Black Magic Probe firmware for ESP32-C5 with network GDB server support.

## I seem esp32-c5 is the best choice and I've been waiting for it for a long time.

## Building

The project requires specific ESP-IDF configuration settings:

### Required Configuration

1. **Partition Table**: Must use `SINGLE_APP_LARGE` (3MB)
   - The firmware with all target support requires more than 1MB
   - Configured in `sdkconfig.defaults`
   - Path in menuconfig: `Partition Table → Single factory app (large), no OTA`

2. **Component Linking**: `WHOLE_ARCHIVE` flag enabled
   - Required to properly link strong symbol definitions from target probe files
   - Without this flag, weak symbol stubs from `target_probe.c` are used instead of actual target implementations
   - Configured in `components/esp32-platform/CMakeLists.txt`

3. **Watchdog Timers**: Disabled (`CONFIG_ESP_INT_WDT=n`, `CONFIG_ESP_TASK_WDT_EN=n`)
   - Black Magic Probe has long-running functions (flash operations, target probing) that exceed default watchdog timeout
   - Disabling prevents spurious watchdog resets during legitimate operations
   - Configured in `sdkconfig.defaults`

These settings are automatically applied from `sdkconfig.defaults` on first build after cloning.

### Build Commands

```bash
# Build ESP32 firmware (frontend is built automatically)
idf.py build
idf.py flash monitor
```

The frontend is automatically built during the ESP-IDF build process via a CMake custom command.

## Frontend Development

The web interface for firmware flashing is located in the `frontend/` directory:

```bash
cd frontend
npm install       # Install dependencies (first time only)
npm run build     # Build and generate C header file (optional - done automatically)
```

The build process:
1. Inlines CSS and JavaScript into HTML
2. Minifies the resulting HTML
3. Generates `frontend/dist/network-http-page.h` with the page as a C string

**Note**: The frontend is automatically rebuilt when you run `idf.py build` if any source files (`src/index.html`, `src/styles.css`, `src/app.js`) have changed. Manual rebuild is only needed for testing frontend changes independently.

### Frontend Testing

To test the web interface without flashing to ESP32:

```bash
cd frontend
npm run dev
```

This automatically generates a test version from your source files with mocked backend, starts a local server, and opens it in your browser. Any changes you make to `src/index.html` will be reflected when you restart the dev server.

See [frontend/README.md](frontend/README.md) for more options.

## Usage

`$ target extended-remote <ip_esp32>:2345`
`$ monitor swdp_scan`
`$ attach 1`

## RTT Support
To enable RTT support, ensure the following:
1. In `CMakeLists.txt`, add the definition `-DENABLE_RTT=1`.
2. Use telnet to connect to the RTT server on port 2346. `$ telnet <ip_esp32> 2346`
