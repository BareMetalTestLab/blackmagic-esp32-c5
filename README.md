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
idf.py build
idf.py flash monitor
```

## Usage

`$ target extended-remote <ip_esp32>:2345`
`$ monitor swdp_scan`
`$ attach 1`

## RTT Support
To enable RTT support, ensure the following:
1. In `CMakeLists.txt`, add the definition `-DENABLE_RTT=1`.
2. Use telnet to connect to the RTT server on port 2346. `$ telnet <ip_esp32> 2346`
