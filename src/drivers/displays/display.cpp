#include "display.h"
#include <Arduino.h>
#include "../storage/storage.h"

#ifdef NO_DISPLAY
DisplayDriver *currentDisplayDriver = &noDisplayDriver;
#endif

#ifdef M5STACK_DISPLAY
DisplayDriver *currentDisplayDriver = &m5stackDisplayDriver;
#endif

#ifdef WT32_DISPLAY
DisplayDriver *currentDisplayDriver = &wt32DisplayDriver;
#endif

#ifdef LED_DISPLAY
DisplayDriver *currentDisplayDriver = &ledDisplayDriver;
#endif

#ifdef OLED_042_DISPLAY
DisplayDriver *currentDisplayDriver = &oled042DisplayDriver;
#endif

#ifdef T_DISPLAY
DisplayDriver *currentDisplayDriver = &tDisplayDriver;
#endif

#ifdef AMOLED_DISPLAY
DisplayDriver *currentDisplayDriver = &amoledDisplayDriver;
#endif

#ifdef DONGLE_DISPLAY
DisplayDriver *currentDisplayDriver = &dongleDisplayDriver;
#endif

#ifdef ESP32_2432S028R
DisplayDriver *currentDisplayDriver = &esp32_2432S028RDriver;
#endif

#ifdef ESP32_2432S028_2USB
DisplayDriver *currentDisplayDriver = &esp32_2432S028RDriver;
#endif

#ifdef T_QT_DISPLAY
DisplayDriver *currentDisplayDriver = &t_qtDisplayDriver;
#endif

#ifdef V1_DISPLAY
DisplayDriver *currentDisplayDriver = &tDisplayV1Driver;
#endif

#ifdef M5STICKC_DISPLAY
DisplayDriver *currentDisplayDriver = &m5stickCDriver;
#endif

#ifdef M5STICKCPLUS_DISPLAY
DisplayDriver *currentDisplayDriver = &m5stickCPlusDriver;
#endif

#ifdef T_HMI_DISPLAY
DisplayDriver *currentDisplayDriver = &t_hmiDisplayDriver;
#endif

#ifdef ST7735S_DISPLAY
DisplayDriver *currentDisplayDriver = &sp_kcDisplayDriver;
#endif

// Screensaver state management
unsigned long lastActivityTime = 0;
uint8_t lastActiveScreen = 0;
bool isScreensaverActive = false;

// Initialize the display
void initDisplay()
{
  currentDisplayDriver->initDisplay();
  lastActivityTime = millis();
  isScreensaverActive = false;
}

// Alternate screen state
void alternateScreenState()
{
  if (isScreensaverActive) {
    updateActivityTime();
    wakeFromScreensaver();
    return;
  }

  updateActivityTime();

  currentDisplayDriver->alternateScreenState();
}

// Alternate screen rotation
void alternateScreenRotation()
{
  if (isScreensaverActive) {
    updateActivityTime();
    wakeFromScreensaver();
    return;
  }

  updateActivityTime();

  currentDisplayDriver->alternateScreenRotation();
}

// Draw the loading screen
void drawLoadingScreen()
{
  currentDisplayDriver->loadingScreen();
}

// Draw the setup screen
void drawSetupScreen()
{
  currentDisplayDriver->setupScreen();
}

// Reset the current cyclic screen to the first one
void resetToFirstScreen()
{
  currentDisplayDriver->current_cyclic_screen = 0;
}

// Switches to the next cyclic screen without drawing it
void switchToNextScreen()
{
  // If screensaver is active, wake from screensaver instead of cycling
  if (isScreensaverActive) {
    updateActivityTime();
    wakeFromScreensaver();
    return;
  }

  updateActivityTime();

  // Normal screen cycling
  currentDisplayDriver->current_cyclic_screen = (currentDisplayDriver->current_cyclic_screen + 1) % currentDisplayDriver->num_cyclic_screens;
}

// Draw the current cyclic screen
void drawCurrentScreen(unsigned long mElapsed)
{
  currentDisplayDriver->cyclic_screens[currentDisplayDriver->current_cyclic_screen](mElapsed);
}

// Animate the current cyclic screen
void animateCurrentScreen(unsigned long frame)
{
  currentDisplayDriver->animateCurrentScreen(frame);
}

// Do LED stuff
void doLedStuff(unsigned long frame)
{
  currentDisplayDriver->doLedStuff(frame);
}

// Update activity time (called on button press or touch)
void updateActivityTime()
{
  lastActivityTime = millis();
}

// Check if screensaver timeout has been reached
void checkScreensaver()
{
  extern TSettings Settings;

  // Don't activate if timeout is 0 (disabled)
  if (Settings.ScreensaverTimeout == 0) {
    return;
  }

  // Don't re-activate if already active
  if (isScreensaverActive) {
    return;
  }

  // Check if timeout exceeded
  unsigned long currentTime = millis();
  unsigned long timeoutMs = (unsigned long)Settings.ScreensaverTimeout * 60 * 1000;

  // Handle millis() overflow (occurs every ~49 days)
  if (currentTime < lastActivityTime) {
    lastActivityTime = currentTime;
    return;
  }

  if (currentTime - lastActivityTime >= timeoutMs) {
    // Activate screensaver
    lastActiveScreen = currentDisplayDriver->current_cyclic_screen;
    isScreensaverActive = true;
    currentDisplayDriver->alternateScreenState();  // Turn off display
    Serial.printf("Screensaver activated after %d minutes of inactivity\n", Settings.ScreensaverTimeout);
  }
}

// Wake from screensaver
void wakeFromScreensaver()
{
  if (!isScreensaverActive) {
    return;
  }

  isScreensaverActive = false;
  currentDisplayDriver->alternateScreenState();  // Turn on display
  currentDisplayDriver->current_cyclic_screen = lastActiveScreen;  // Restore last screen
  Serial.println("Screensaver deactivated - display restored");
}
