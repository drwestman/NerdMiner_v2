# NerdMiner v2 - Mining Flow Flowchart

This document provides a comprehensive flowchart of the NerdMiner v2 mining operation, showing the flow from startup to mining execution.

## System Architecture Overview

NerdMiner v2 uses FreeRTOS tasks running on ESP32 microcontrollers to implement Bitcoin solo mining:
- **Monitor Task** (Core 1, Priority 5) - Display updates and statistics
- **Stratum Worker Task** (Core 1, Priority 4) - Pool communication and job management
- **Miner Tasks** (Core 0/1, Priority 1-3) - SHA-256 mining execution

---

## Main System Flow

```mermaid
flowchart TD
    Start([Power On / Reset]) --> Init[Initialize Hardware]
    Init --> InitDisplay[Initialize Display]
    InitDisplay --> ShowSplash[Show Loading Screen]
    ShowSplash --> InitWiFi[Initialize WiFi Manager]
    
    InitWiFi --> CheckWiFi{WiFi Configured?}
    CheckWiFi -->|No| CaptivePortal[Start Captive Portal<br/>AP: NerdMinerAP]
    CaptivePortal --> UserConfig[User Configures:<br/>- WiFi Credentials<br/>- Pool Address<br/>- BTC Wallet]
    UserConfig --> SaveConfig[Save Config to SPIFFS]
    SaveConfig --> ConnectWiFi
    
    CheckWiFi -->|Yes| ConnectWiFi[Connect to WiFi]
    ConnectWiFi --> CreateTasks[Create FreeRTOS Tasks]
    
    CreateTasks --> MonitorTask[Monitor Task<br/>Core 1, Priority 5]
    CreateTasks --> StratumTask[Stratum Worker Task<br/>Core 1, Priority 4]
    CreateTasks --> MinerTask1[Miner Task 0<br/>HW/SW SHA-256]
    CreateTasks --> MinerTask2[Miner Task 1<br/>SW SHA-256<br/>Dual-core only]
    
    MonitorTask --> MonitorLoop[Monitor Loop]
    StratumTask --> StratumLoop[Stratum Loop]
    MinerTask1 --> MinerLoop[Miner Loop]
    MinerTask2 --> MinerLoop
    
    style Start fill:#90EE90
    style MonitorTask fill:#87CEEB
    style StratumTask fill:#FFB6C1
    style MinerTask1 fill:#FFD700
    style MinerTask2 fill:#FFD700
```

---

## Stratum Worker Task Flow

```mermaid
flowchart TD
    StratumStart([Stratum Task Start]) --> CheckWiFiStatus{WiFi<br/>Connected?}
    
    CheckWiFiStatus -->|No| WaitReconnect[Set Status: Connecting<br/>Stop Mining Jobs<br/>Reconnect WiFi]
    WaitReconnect --> Delay1[Delay 5s]
    Delay1 --> CheckWiFiStatus
    
    CheckWiFiStatus -->|Yes| CheckPool{Pool<br/>Connected?}
    
    CheckPool -->|No| ConnectPool[Connect to Pool Server<br/>Resolve DNS if needed]
    ConnectPool --> ConnectSuccess{Connection<br/>Successful?}
    ConnectSuccess -->|No| RandomDelay[Random Delay 1-60s]
    RandomDelay --> CheckWiFiStatus
    ConnectSuccess -->|Yes| CheckSub
    
    CheckPool -->|Yes| CheckSub{Miner<br/>Subscribed?}
    
    CheckSub -->|No| Subscribe[STEP 1: mining.subscribe<br/>Send NerdMinerV2 version]
    Subscribe --> SubSuccess{Subscribe<br/>OK?}
    SubSuccess -->|No| CloseSocket[Close Socket<br/>Stop Mining Jobs]
    CloseSocket --> CheckWiFiStatus
    
    SubSuccess -->|Yes| Authorize[STEP 2: mining.authorize<br/>Send wallet & password]
    Authorize --> SuggestDiff[STEP 3: mining.set_difficulty<br/>Suggest difficulty]
    SuggestDiff --> SetSubscribed[Set isMinerSuscribed = true<br/>Record timestamp]
    SetSubscribed --> CheckSub
    
    CheckSub -->|Yes| CheckInactivity{Pool<br/>Inactive?}
    CheckInactivity -->|Yes| RestartConn[Pool timeout detected<br/>Close socket<br/>Reset subscription]
    RestartConn --> CheckWiFiStatus
    
    CheckInactivity -->|No| CheckTimeout{10 min<br/>no jobs?}
    CheckTimeout -->|Yes| RestartConn
    
    CheckTimeout -->|No| ReadMessages{Messages<br/>from Pool?}
    
    ReadMessages -->|No| CheckJobs{Jobs<br/>Available?}
    
    ReadMessages -->|Yes| ParseMethod[Parse Stratum Method]
    ParseMethod --> MethodType{Method<br/>Type?}
    
    MethodType -->|mining.notify| ParseNotify[Parse Job Notification<br/>Extract: job_id, coinbase,<br/>merkle_branch, ntime, nbits]
    ParseNotify --> ClearJobQueue[Clear Job Request Queues<br/>SW and HW]
    ClearJobQueue --> IncrementTemplate[Increment Templates Counter<br/>Increment job_pool ID]
    IncrementTemplate --> BuildHeader[Build Block Header<br/>Construct 80-byte header]
    BuildHeader --> ComputeMidstate[Compute Midstate<br/>SHA-256 of first 64 bytes]
    ComputeMidstate --> CheckJobs
    
    MethodType -->|mining.set_difficulty| UpdateDifficulty[Update Pool Difficulty<br/>Convert to target]
    UpdateDifficulty --> ReadMessages
    
    MethodType -->|Response| CheckSubmitResp{Is Submit<br/>Response?}
    CheckSubmitResp -->|Yes| ProcessSubmit[Process Submit Result<br/>Update valid/shares count]
    ProcessSubmit --> ReadMessages
    CheckSubmitResp -->|No| ReadMessages
    
    CheckJobs --> FillQueues[Fill Job Queues<br/>SW: 4 jobs × 4096 nonces<br/>HW: 4 jobs × 16384 nonces]
    FillQueues --> ProcessResults{Mining<br/>Results?}
    
    ProcessResults -->|Yes| GetResult[Get Mining Result<br/>Check difficulty]
    GetResult --> CheckDiff{Result diff ><br/>Pool diff?}
    
    CheckDiff -->|Yes| SubmitShare[STEP 4: mining.submit<br/>Submit nonce to pool]
    SubmitShare --> RecordSubmit[Record Submission<br/>Check if valid share]
    RecordSubmit --> ProcessResults
    
    CheckDiff -->|No| ProcessResults
    ProcessResults -->|No| KeepAlive{Time for<br/>Keep-Alive?}
    
    KeepAlive -->|Yes| SendKeepAlive[Send mining.set_difficulty<br/>to keep socket alive]
    SendKeepAlive --> SmallDelay[Delay 40-50ms]
    SmallDelay --> CheckWiFiStatus
    
    KeepAlive -->|No| SmallDelay
    
    style StratumStart fill:#90EE90
    style Subscribe fill:#87CEEB
    style Authorize fill:#87CEEB
    style ParseNotify fill:#FFB6C1
    style SubmitShare fill:#FFD700
```

---

## Miner Worker Task Flow

```mermaid
flowchart TD
    MinerStart([Miner Task Start<br/>minerWorkerSw/Hw]) --> InitMiner[Initialize Miner<br/>Task ID: 0 or 1]
    
    InitMiner --> MinerLoop{Job<br/>Available?}
    
    MinerLoop -->|No| WaitJob[Wait for Job<br/>Delay 2ms]
    WaitJob --> MinerLoop
    
    MinerLoop -->|Yes| GetJob[Get Job from Queue<br/>Lock mutex]
    GetJob --> ExtractJob[Extract Job Data:<br/>- nonce_start<br/>- nonce_count<br/>- midstate<br/>- block header<br/>- difficulty]
    
    ExtractJob --> StartHashing[Start Nonce Loop<br/>Test nonce_count values]
    
    StartHashing --> NonceLoop{For each<br/>nonce}
    
    NonceLoop --> InsertNonce[Insert nonce into<br/>block header bytes 76-79]
    InsertNonce --> SHA256d[Compute SHA-256d<br/>Double SHA-256 hash]
    
    SHA256d --> HWorSW{HW or SW<br/>SHA-256?}
    
    HWorSW -->|Hardware| HwAccel[Use ESP32 S2/S3/C3<br/>Hardware SHA-256 DMA]
    HwAccel --> CheckHash
    
    HWorSW -->|Software| SwImpl[Software SHA-256<br/>with midstate optimization]
    SwImpl --> CheckHash[Check Hash Result]
    
    CheckHash --> CalcDiff[Calculate Difficulty<br/>from hash]
    CalcDiff --> CompareDiff{Hash diff ><br/>current best?}
    
    CompareDiff -->|Yes| UpdateBest[Update Best Result:<br/>- nonce<br/>- difficulty<br/>- hash]
    UpdateBest --> CheckJobChange
    
    CompareDiff -->|No| CheckJobChange{New job<br/>arrived?}
    
    CheckJobChange -->|Yes| AbortJob[Abort Current Job<br/>Save partial results]
    AbortJob --> SubmitResult
    
    CheckJobChange -->|No| CheckComplete{All nonces<br/>tested?}
    
    CheckComplete -->|No| NonceLoop
    CheckComplete -->|Yes| SubmitResult[Submit Result to Queue<br/>Lock mutex]
    
    SubmitResult --> FeedWDT[Feed Watchdog Timer<br/>Every 8 iterations]
    FeedWDT --> MinerLoop
    
    style MinerStart fill:#90EE90
    style SHA256d fill:#FFD700
    style HwAccel fill:#87CEEB
    style SwImpl fill:#87CEEB
    style UpdateBest fill:#90EE90
```

---

## Monitor Task Flow

```mermaid
flowchart TD
    MonitorStart([Monitor Task Start]) --> InitMonitor[Initialize Monitor<br/>Set update interval]
    
    InitMonitor --> MonitorLoop[Monitor Loop]
    
    MonitorLoop --> UpdateTime[Update System Time<br/>NTP sync if needed]
    UpdateTime --> ReadStats[Read Mining Statistics:<br/>- Hashrate<br/>- Shares/Valid<br/>- Uptime<br/>- Temperature<br/>- Best difficulty]
    
    ReadStats --> UpdateScreen{Screen<br/>Active?}
    
    UpdateScreen -->|Yes| GetScreenMode{Current<br/>Screen Mode?}
    
    GetScreenMode -->|Mining Stats| DisplayMining[Display:<br/>- Current hashrate<br/>- Valid shares<br/>- Temperature<br/>- Pool status]
    GetScreenMode -->|Clock| DisplayClock[Display:<br/>- Current time<br/>- BTC price<br/>- Block height]
    GetScreenMode -->|Global Stats| DisplayGlobal[Display:<br/>- Network difficulty<br/>- Global hashrate<br/>- Block info]
    GetScreenMode -->|BTC Price| DisplayPrice[Display:<br/>- BTC price chart<br/>- Market data]
    GetScreenMode -->|Blank| DisplayBlank[Display Off<br/>Power saving mode]
    
    DisplayMining --> CheckButton
    DisplayClock --> CheckButton
    DisplayGlobal --> CheckButton
    DisplayPrice --> CheckButton
    DisplayBlank --> CheckButton
    
    UpdateScreen -->|No| CheckButton{Button<br/>Pressed?}
    
    CheckButton -->|Click| NextScreen[Switch to Next Screen]
    NextScreen --> UpdateInterval
    
    CheckButton -->|Double Click| RotateScreen[Rotate Screen Orientation]
    RotateScreen --> UpdateInterval
    
    CheckButton -->|Triple Click| TogglePower[Toggle Screen Power]
    TogglePower --> UpdateInterval
    
    CheckButton -->|Long Press 5s| ResetConfig[Reset Configuration<br/>Clear WiFi settings<br/>Restart ESP32]
    ResetConfig --> MonitorStart
    
    CheckButton -->|No| UpdateInterval[Wait Update Interval<br/>~1 second]
    
    UpdateInterval --> SaveStatsCheck{Time to<br/>save stats?}
    
    SaveStatsCheck -->|Yes| SaveNVS[Save Statistics to NVS:<br/>- Total shares<br/>- Total hashes<br/>- Best difficulty<br/>- Uptime]
    SaveNVS --> UpdateLED
    
    SaveStatsCheck -->|No| UpdateLED[Update LED Status<br/>LED-only devices]
    
    UpdateLED --> MonitorLoop
    
    style MonitorStart fill:#90EE90
    style DisplayMining fill:#87CEEB
    style SaveNVS fill:#FFB6C1
    style ResetConfig fill:#FF6B6B
```

---

## Data Flow Between Tasks

```mermaid
flowchart LR
    subgraph "Stratum Worker Task"
        S1[Connect to Pool]
        S2[Parse mining.notify]
        S3[Build Block Header]
        S4[Submit Shares]
    end
    
    subgraph "Shared Memory"
        M1[(mMiner Data<br/>Block Header<br/>Target<br/>Merkle Root)]
        M2[(Job Queues<br/>SW Queue<br/>HW Queue)]
        M3[(Result Queue<br/>Nonces & Hashes)]
        M4[(mMonitor Data<br/>Statistics)]
    end
    
    subgraph "Miner Tasks"
        W1[Miner Task 0<br/>Get Job]
        W2[Miner Task 1<br/>Get Job]
        W3[SHA-256 Mining]
        W4[Submit Results]
    end
    
    subgraph "Monitor Task"
        D1[Read Statistics]
        D2[Update Display]
        D3[Save to NVS]
    end
    
    S2 --> M1
    S3 --> M2
    M2 --> W1
    M2 --> W2
    W1 --> W3
    W2 --> W3
    W3 --> W4
    W4 --> M3
    M3 --> S4
    S4 --> M4
    W4 --> M4
    M4 --> D1
    D1 --> D2
    D1 --> D3
    
    style M1 fill:#FFE4B5
    style M2 fill:#FFE4B5
    style M3 fill:#FFE4B5
    style M4 fill:#FFE4B5
```

---

## Key Data Structures

### miner_data
```cpp
typedef struct {
    uint8_t bytearray_target[32];        // Mining difficulty target
    uint8_t bytearray_pooltarget[32];    // Pool difficulty target
    uint8_t merkle_result[32];           // Merkle root
    uint8_t bytearray_blockheader[128];  // Bitcoin block header
} miner_data;
```

### mining_job
```cpp
typedef struct {
    String job_id;
    String prev_block_hash;
    String coinb1, coinb2;     // Coinbase parts
    String nbits;              // Difficulty bits
    JsonArray merkle_branch;   // Merkle branch
    String version;
    uint32_t target;
    String ntime;
    bool clean_jobs;
} mining_job;
```

### mining_subscribe
```cpp
typedef struct {
    String sub_details;
    String extranonce1;
    String extranonce2;
    int extranonce2_size;
    char wName[80];  // Wallet address
    char wPass[20];  // Pool password
} mining_subscribe;
```

---

## Mining Process Details

### Block Header Construction (80 bytes)
```
Bytes  0-3:   Version
Bytes  4-35:  Previous Block Hash
Bytes 36-67:  Merkle Root (includes coinbase with extranonce)
Bytes 68-71:  nTime
Bytes 72-75:  nBits (difficulty)
Bytes 76-79:  Nonce (tested by miner)
```

### SHA-256d Mining
1. **Midstate Optimization**: Pre-compute SHA-256 of first 64 bytes
2. **Nonce Testing**: Iterate through nonce space (4096 or 16384 per job)
3. **Double Hash**: SHA-256(SHA-256(block_header))
4. **Difficulty Check**: Compare hash to pool target
5. **Share Submission**: If hash meets difficulty, submit to pool

### Nonce Space Partitioning
- **Software Mining**: 4096 nonces per job
- **Hardware Mining**: 16,384 nonces per job
- **Dual-Core**: Task 0 and Task 1 test different nonce ranges
- **Random Nonce**: Optional random nonce selection for variety

---

## Performance Characteristics

### Typical Hashrates
- **ESP32 (Software SHA-256)**: ~10-15 kH/s per core
- **ESP32-S2/S3 (Hardware SHA-256)**: ~50-80 kH/s
- **ESP32-C3 (Hardware SHA-256)**: ~40-70 kH/s
- **Dual-Core ESP32**: ~25-30 kH/s total

### Watchdog Timers
- **General WDT**: 3 seconds (for all tasks except mining)
- **Miner WDT**: 15 minutes (mining tasks only)
- **Core 0 WDT**: Disabled (mining fully utilizes core)

### Task Priorities
1. **Monitor Task**: Priority 5 (highest) - Ensures UI responsiveness
2. **Stratum Worker**: Priority 4 - Timely pool communication
3. **Miner HW Task**: Priority 3 - Hardware-accelerated mining
4. **Miner SW Tasks**: Priority 1 - Software mining

---

## Error Handling and Recovery

### Connection Failures
- WiFi disconnect → Auto-reconnect with 5s retry
- Pool disconnect → Random 1-60s delay before retry
- Pool inactivity (2min) → Socket reset and reconnection
- No jobs (10min) → Full connection reset

### Mining Failures
- Invalid job data → Skip and continue
- Job timeout → Abort current work, get new job
- Difficulty too low → Auto-suggest higher difficulty

### Display and Buttons
- Button long press (5s) → Factory reset
- Display blank mode → Power saving
- Touch/button error → Restart display driver

---

## Configuration Options

### Pool Configuration
- **Pool Address**: Stratum pool URL
- **Pool Port**: Typically 3333 or 21496
- **BTC Wallet**: Bitcoin address for payouts
- **Pool Password**: Usually "x" or worker name

### WiFi Configuration
- **SSID**: WiFi network name
- **Password**: WiFi password
- **Fallback**: Captive portal mode if connection fails

### Display Options
- **Screen Rotation**: 0°, 90°, 180°, 270°
- **Brightness**: Auto or manual
- **Screen Timeout**: Blank screen for power saving

---

## References

- **Main Entry**: `src/NerdMinerV2.ino.cpp`
- **Mining Logic**: `src/mining.cpp` and `src/mining.h`
- **Stratum Protocol**: `src/stratum.cpp` and `src/stratum.h`
- **Display Drivers**: `src/drivers/displays/`
- **WiFi Manager**: `src/wManager.cpp` and `src/wManager.h`
- **Monitor System**: `src/monitor.cpp` and `src/monitor.h`

---

*Generated: 2026-01-17*
*NerdMiner v2 - Open Source ESP32 Bitcoin Miner*
