# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

NerdMiner v2 is an open-source ESP32-based Bitcoin solo mining project. It implements the Stratum mining protocol to communicate with mining pools and performs SHA-256 mining operations on ESP32 microcontrollers. This is primarily an educational project to learn about Bitcoin mining, with support for 30+ different ESP32 board variants.

## Build System (PlatformIO)

This project uses PlatformIO, not Arduino IDE.

### Common Build Commands

```bash
# Build all environments (builds 30+ board configurations)
pio run

# Build specific board environment
pio run -e NerdminerV2                    # TTGO T-Display S3
pio run -e ESP32-devKitv1                 # Generic ESP32-WROOM
pio run -e NerdminerV2-S3-AMOLED          # AMOLED display variant
pio run -e M5Stick-CPlus                  # M5Stick variant

# Upload to device
pio run -e NerdminerV2 -t upload

# Monitor serial output
pio run -e NerdminerV2 -t monitor

# Clean build artifacts
pio run -t clean
```

### Build Environment Selection

The repository supports 30+ board variants defined in `platformio.ini`. Each environment has:
- Board-specific compiler flags (e.g., `-D NERDMINER_V2=1`)
- Custom partition schemes (typically `huge_app.csv`)
- Display driver selection via preprocessor defines
- Hardware-specific pin mappings

Key environments:
- `NerdminerV2` - Main TTGO T-Display S3
- `ESP32-devKitv1` - Generic ESP32 (LED only, no display)
- `ESP32-2432S028R` - ESP32-2432S028 "Cheap Yellow Display" (2.4"/2.8" TFT LCD)
- `esp32cam` - ESP32-CAM variant
- `M5Stick-C*` - M5Stack devices
- `NerdminerV2-S3-AMOLED*` - AMOLED display variants

### Hardware Identification

**Current Hardware Setup:**
- **Board Model**: ESP32-2432S028 (Model: ESP32-32E N4)
- **Display**: 2.4" or 2.8" ILI9341 TFT LCD (240x320 pixels)
- **Chip**: ESP32-D0WD-V3 (revision v3.1)
- **PlatformIO Environment**: `ESP32-2432S028R`
- **Build Command**: `pio run -e ESP32-2432S028R -t upload`

**Important**: Always use the `ESP32-2432S028R` environment, NOT `ESP32-devKitv1` (which is for LED-only boards without displays).

### Build Scripts

- `auto_firmware_version.py` - Injects git version into firmware at build time
- `post_build_merge.py` - Merges bootloader, partitions, and app into factory binaries after build
  - Creates `0x0_firmware.bin` (factory image)
  - Creates `0x10000_firmware.bin` (OTA update image)

## Architecture Overview

### Core Components

**Main Entry Point** (`src/NerdMinerV2.ino.cpp`)
- Initializes hardware, display, WiFi, and creates FreeRTOS tasks
- Sets up watchdog timers (3s for general, 15min for mining)
- Configures button handlers for navigation and configuration reset
- Disables Core 0 WDT as mining fully utilizes it

**FreeRTOS Task Structure**
1. **Monitor Task** (Core 1, priority 5) - UI updates, statistics display
2. **Stratum Worker Task** (Core 1, priority 4) - Pool communication
3. **Miner Tasks** (Core 0/1, priority 1-3) - SHA-256 mining
   - Uses hardware SHA-256 acceleration on supported chips (S2, S3, C3)
   - Falls back to software SHA-256 on ESP32 classic
   - Dual-core devices run one task per core

**Mining System** (`src/mining.cpp`, `src/mining.h`)
- Implements Bitcoin block header hashing (SHA-256d)
- `minerWorkerHw()` - Hardware-accelerated SHA-256 (ESP32-S2/S3/C3)
- `minerWorkerSw()` - Software SHA-256 implementation
- Uses midstate optimization to reduce computation
- Nonce searching with configurable step sizes:
  - Software: 4096 nonces per job
  - Hardware: 16384 nonces per job
- Validates shares against pool difficulty target

**Stratum Protocol** (`src/stratum.cpp`, `src/stratum.h`)
- Implements Stratum V1 mining protocol
- Handles mining.subscribe, mining.authorize, mining.notify, mining.submit
- JSON-RPC communication over TCP
- Supports low-difficulty pools for ESP32 miners
- Automatic reconnection and job handling

**Display Drivers** (`src/drivers/displays/`)
- Abstraction layer supporting 15+ display types
- `DisplayDriver` struct with function pointers for screen operations
- Board-specific implementations selected via preprocessor flags
- Cyclic screen rotation with button navigation
- **Screen Modes** (varies by board):
  - Mining stats screen - Hashrate, shares, temperature
  - Clock screen - Time, BTC price, block height
  - Global stats screen - Network difficulty, global hashrate
  - BTC price screen - Bitcoin price display
  - **Blank screen** - Powers off display to save energy
- **Blank Screen Feature** (added 2026-01-15):
  - Available on all boards with physical displays (TFT/OLED/AMOLED)
  - NOT available on LED-only boards (noDisplayDriver, ledDisplayDriver)
  - Last screen in cyclic rotation - pressing button blanks display
  - Pressing button again wakes display and returns to first screen
  - Implementation: `switchToNextScreen()` in `src/drivers/displays/display.cpp:105-126`
  - Calls `alternateScreenState()` to power off/on the backlight
  - Each display driver has a `_BlankScreen()` function (empty stub)

**WiFi Management** (`src/wManager.cpp`, `src/wManager.h`)
- Uses WiFiManager library for captive portal configuration
- Access Point mode for initial setup (SSID: NerdMinerAP, Pass: MineYourCoins)
- Configuration stored in SPIFFS/NVS
- SD card config support via `config.json`
- Custom AP name feature for certain boards (TDisplayS3, DevKit)

**Storage Systems**
- NVS (Non-Volatile Storage) - Mining statistics persistence
- SPIFFS - WiFi/pool configuration
- SD Card (optional) - Config loading from `config.json`

### Key Data Structures

```cpp
typedef struct {
    uint8_t bytearray_target[32];        // Mining difficulty target
    uint8_t bytearray_pooltarget[32];    // Pool difficulty target
    uint8_t merkle_result[32];           // Merkle root after coinbase insertion
    uint8_t bytearray_blockheader[128];  // Bitcoin block header to hash
} miner_data;

typedef struct {
    String job_id;
    String prev_block_hash;
    String coinb1, coinb2;               // Coinbase transaction parts
    String nbits;                        // Difficulty bits
    JsonArray merkle_branch;             // Merkle branch for block construction
    String version;
    uint32_t target;
    String ntime;
    bool clean_jobs;
} mining_job;
```

### Hardware Abstraction

**Board Detection Pattern**
1. PlatformIO environment defines board type (e.g., `-D NERDMINER_V2=1`)
2. `src/drivers/displays/display.cpp` selects appropriate display driver
3. Pin mappings configured via build flags in `platformio.ini`
4. Display driver initialized with board-specific settings

**SHA-256 Implementation Selection**
- `#define HARDWARE_SHA265` enables HW acceleration
- ESP32-S2/S3/C3: Uses `sha/sha_dma.h` HAL for hardware SHA
- ESP32 classic: Software implementation with optional parallel engine
- Midstate caching reduces redundant computation

### Critical Subsystems

**Button Handling**
- One-button devices: Click=next screen, Double=rotate, Triple=screen off, Hold 5s=reset config
- Two-button devices: Top=screen on/off/rotate, Bottom=next screen/reset config
- Touch support for touchscreen variants

**LED Indicators** (for devices without displays)
- Different patterns indicate miner status
- Implemented in `ledDisplayDriver.cpp`

**Configuration Persistence**
- Settings loaded on boot from SPIFFS or SD card
- SD card config.json overrides SPIFFS if present
- Hold button during boot to reset configuration

## Development Guidelines

### Adding a New Board

1. Add environment to `platformio.ini` with board definition and build flags
2. Create display driver in `src/drivers/displays/` if needed
3. Define pin mappings via `-D PIN_*` flags
4. Add board detection in `src/drivers/displays/display.cpp`
5. Update `boards/` JSON if custom board definition needed
6. Test bootloader detection in `post_build_merge.py`

### Modifying Mining Logic

- Mining core is in `src/mining.cpp:minerWorkerSw()` and `minerWorkerHw()`
- Nonce ranges split across tasks to avoid duplicate work
- Critical section: Share submission must be atomic
- Watchdog timer must be fed regularly (15min timeout)

### Display System

- All display operations go through `DisplayDriver` function pointers
- Screens update cyclically via `runMonitor()` task
- Use `mMonitor` global structure for sharing mining stats
- Coordinate with monitor task via mutex if adding new shared data

### Memory Constraints

- ESP32 classic is memory-constrained (use reduced stack sizes)
- S3/S2 variants have PSRAM (can use larger stacks)
- Partition scheme is `huge_app.csv` to fit large compiled binary
- TFT_eSPI and display libraries consume significant RAM

## Common Development Workflows

### Testing Configuration Changes

1. Build firmware: `pio run -e <environment>`
2. Flash to device: `pio run -e <environment> -t upload`
3. Monitor output: `pio run -e <environment> -t monitor`
4. Hold button 5s to factory reset if needed

### Debugging Mining Issues

- Enable debug mining: Add `-D DEBUG_MINING=1` to build_flags
- Monitor serial output at 115200 baud
- Check share submissions and pool responses
- Verify nonce ranges don't overlap between tasks

### Adding a New Screen Mode

1. Create screen update function with signature `void screenName(unsigned long mElapsed)`
2. Add to cyclic_screens array in display driver
3. Increment `num_cyclic_screens`
4. Screen cycles on button press via `switchToNextScreen()`

## Partition Scheme

Uses custom `huge_app.csv` partition table:
- Typically 2x ~4.5MB app partitions for OTA updates
- SPIFFS partition for configuration (~6.8MB on 16MB flash)
- Bootloader, NVS, partition table in first sectors

## Dependencies

Key libraries (defined in platformio.ini `lib_deps`):
- WiFiManager - Captive portal configuration
- ArduinoJson - JSON parsing for Stratum protocol
- OneButton - Button debouncing and gestures
- NTPClient - Time synchronization
- TFT_eSPI / LovyanGFX / U8g2 - Display libraries (board-dependent)
- FastLED - LED control for RGB indicators
- OpenFontRender - Custom font rendering

## Testing

No automated test suite exists. Testing is manual:
1. Flash firmware to physical device
2. Configure via captive portal
3. Verify mining operation via serial monitor
4. Check pool statistics on pool website
5. Test button interactions and screen cycling

## Serial Communication

- Default baud: 115200
- Outputs mining statistics, share submissions, pool responses
- Use for debugging Stratum protocol and mining operations
- Exception decoder available via monitor_filters in platformio.ini

## Custom Features & Modifications

### Blank Screen Feature (2026-01-15)

**Purpose**: Power-saving feature that turns off the display backlight when cycling past the last screen.

**Implementation Details**:
- **Modified Files**:
  - `src/drivers/displays/display.cpp` - Updated `switchToNextScreen()` function
  - 11 display driver files - Added blank screen function and updated cyclic arrays
    - `amoledDisplayDriver.cpp`, `tDisplayDriver.cpp`, `tDisplayV1Driver.cpp`
    - `dongleDisplayDriver.cpp`, `esp23_2432s028r.cpp`, `t_qtDisplayDriver.cpp`
    - `t_hmiDisplayDriver.cpp`, `oled042DisplayDriver.cpp`, `sp_kcDisplayDriver.cpp`
    - `m5stickCDriver.cpp`, `m5stickCPlusDriver.cpp`

**How It Works**:
1. Blank screen is added as the last element in each display's cyclic screen array
2. When button is pressed to switch to blank screen (last screen):
   - `switchToNextScreen()` calls `alternateScreenState()` to turn off backlight
   - Sets current screen to last index
3. When button is pressed again from blank screen:
   - `alternateScreenState()` turns backlight back on
   - Resets to first screen (index 0)

**User Experience** (ESP32-2432S028R):
- Press button: Miner → Clock → Global Hash → BTC Price → **Blank** (display OFF)
- Press button from blank: **Miner** (display ON)

**Excluded Drivers**:
- `noDisplayDriver.cpp` - LED-only, no physical display
- `ledDisplayDriver.cpp` - LED-only
- Other minimal display drivers without full screen support

**Testing Notes**:
- Successfully tested on ESP32-2432S028R (ESP32-D0WD-V3)
- Screen powers off completely when entering blank screen
- Backlight restores and returns to first screen on wake
- Build size impact: ~100 bytes per driver (minimal)
