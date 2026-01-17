# Phase 1 Optimization Testing Guide

## Changes Implemented

### 1.1: Decouple Pool Data Refresh from Screen Changes
**File:** `src/drivers/displays/esp32_2432s028r.cpp`
**Change:** Removed `hasChangedScreen` trigger from `printPoolData()` condition
**Impact:** Pool data refreshes only on timer, not on every screen switch

### 1.2: Screen Switch Debouncing
**File:** `src/drivers/displays/display.cpp`
**Change:** Added 300ms debounce to `switchToNextScreen()`
**Impact:** Ignores rapid button presses, prevents queued screen redraws

### 1.3: Explicit Flag Management
**Files:** `display.cpp`, `esp32_2432s028r.cpp`
**Change:** `hasChangedScreen` flag now set explicitly in handlers, removed automatic toggle
**Impact:** Better control over full screen redraws

---

## Testing Instructions

### 1. Flash the Firmware

```bash
cd /home/david/NerdMiner_v2
pio run -e ESP32-2432S028R -t upload
```

### 2. Monitor Serial Output

```bash
pio run -e ESP32-2432S028R -t monitor
```

**Look for:**
- `[DISPLAY] Switched to screen X` - Confirms screen switches
- `[DISPLAY] Screen switch debounced (too soon)` - Confirms debouncing works
- No more pool API calls on screen switches (only time-based)

### 3. Manual Performance Testing

#### Test A: Screen Switch Latency
**Before:** ~500-2000ms per switch (with HTTP calls)
**Target:** <500ms per switch

**Procedure:**
1. Touch right side of screen to switch forward
2. Count seconds until screen fully updates
3. Repeat 10 times and average

**Expected Result:**
- First switch after boot: ~500-1000ms (pool data load)
- Subsequent switches: ~200-400ms (no pool data)
- Much faster than before!

#### Test B: Rapid Button Press (Debouncing)
**Before:** Multiple screen switches queued, UI unresponsive
**Target:** Only one switch per 300ms, UI responsive

**Procedure:**
1. Rapidly tap right side of screen 5 times in 1 second
2. Observe how many screen changes occur

**Expected Result:**
- Only 2-3 screen changes (1 per 300ms)
- Serial monitor shows "debounced" messages
- UI doesn't freeze or queue switches

#### Test C: Pool Data Independence
**Before:** Pool data refreshed on every screen switch
**Target:** Pool data refreshes only on timer (UPDATE_POOL_min)

**Procedure:**
1. Switch screens multiple times rapidly
2. Monitor serial output for HTTP requests

**Expected Result:**
- No HTTP requests triggered by screen switches
- Pool data requests only after UPDATE_POOL_min minutes
- Screen switches feel instant

#### Test D: Touch Area Tests
**Test all touch zones:**
1. **Left side (x<160):** Previous screen - should work with flag set
2. **Right side (x>160):** Next screen - should use debounced function
3. **Bottom pool area (109<x<211, 185<y<241):** Toggle pool color
4. **Top-right corner (x>235, y<16):** Screen on/off

### 4. Long-Running Stability Test

**Procedure:**
1. Let device run for 1 hour
2. Switch screens every 5 minutes
3. Monitor for crashes or memory leaks

**Expected Result:**
- No crashes
- Heap memory stable (check with `ESP.getFreeHeap()`)
- All features working

### 5. Edge Case Testing

#### A. Network Latency
1. Simulate slow network (if possible)
2. Switch screens during HTTP call
3. Verify UI remains responsive

#### B. First Boot
1. Factory reset device
2. Configure WiFi
3. Test screen switches before pool connection
4. Should work without crashes

---

## Performance Benchmarks

### Baseline (Before Optimization)
Record these metrics BEFORE Phase 1:
- [ ] Screen switch latency: _______ ms (avg of 10 switches)
- [ ] First switch after boot: _______ ms
- [ ] HTTP calls per screen switch: _______ calls
- [ ] Debounce effectiveness: _______ (switches accepted in 1 sec)

### Phase 1 (After Optimization)
Record these metrics AFTER Phase 1:
- [ ] Screen switch latency: _______ ms (avg of 10 switches)
- [ ] First switch after boot: _______ ms  
- [ ] HTTP calls per screen switch: _______ calls
- [ ] Debounce effectiveness: _______ (switches accepted in 1 sec)

### Expected Improvements
- Screen switch latency: **40-50% reduction** (no HTTP blocking)
- HTTP calls on switches: **100% reduction** (0 calls)
- Debounce: **Max 3 switches/second** (vs unlimited before)

---

## Serial Output Examples

### Successful Screen Switch (After Phase 1)
```
[DISPLAY] Switched to screen 1
>>> Completed 0 share(s), 1250 Khashes, avg. hashrate 12.5 KH/s
```

### Debounced Switch Attempt
```
[DISPLAY] Screen switch debounced (too soon)
```

### Pool Data Refresh (Timer-Based)
```
[WORKER] Received message from pool
  Receiving: {"method":"mining.notify",...}
```

---

## Troubleshooting

### Issue: Screen doesn't switch at all
**Check:**
- Serial monitor for errors
- Touch calibration (`ETOUCH_CS` pin defined)
- hasChangedScreen flag being set

### Issue: Screens switch too slowly
**Check:**
- Network latency (pool data may still be loading)
- Large image pushes (expected, Phase 2 will optimize)
- CPU usage in serial monitor

### Issue: Debouncing too aggressive
**Solution:**
- Adjust `SCREEN_SWITCH_DEBOUNCE_MS` in `display.cpp` (currently 300ms)
- Lower value = more responsive, less debouncing
- Higher value = more debouncing, less responsive

### Issue: Pool data not updating
**Check:**
- `UPDATE_POOL_min` timer (default 5-10 minutes)
- Network connectivity
- Serial output for HTTP errors

---

## Rollback Instructions

If Phase 1 causes issues:

```bash
cd /home/david/NerdMiner_v2
git checkout main
pio run -e ESP32-2432S028R -t upload
```

Or revert specific commits:
```bash
git revert c18ae9e
```

---

## Next Steps

If Phase 1 testing is successful:
- [ ] Document actual performance gains
- [ ] Proceed to Phase 2 (Async HTTP)
- [ ] Consider Phase 2 (Double Buffering)

If issues found:
- [ ] Document issues in GitHub issue
- [ ] Adjust parameters (debounce timing, etc.)
- [ ] Retest before proceeding

---

## Success Criteria

Phase 1 is successful if:
- [x] Firmware builds without errors
- [ ] Screen switches work on all touch zones
- [ ] No HTTP calls triggered by screen switches
- [ ] Debouncing prevents rapid switches
- [ ] No crashes or memory leaks in 1-hour test
- [ ] User perceives faster screen switching

---

**Testing Date:** _________________  
**Tester:** _________________  
**Result:** ⬜ PASS  ⬜ FAIL  ⬜ NEEDS ADJUSTMENT

**Notes:**
_____________________________________________________________
_____________________________________________________________
_____________________________________________________________
