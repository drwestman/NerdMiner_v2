#include "display.h"
#include <Arduino.h>
#include "../storage/storage.h"

// External settings reference
extern TSettings Settings;

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
static unsigned long lastActivityTime = 0;
static uint8_t lastActiveScreen = 0;
bool isScreensaverActive = false;  // Exposed via header
static SemaphoreHandle_t screensaverMutex = NULL;

// Initialize the display
void initDisplay()
{
  // Create mutex for thread-safe access to screensaver state
  if (screensaverMutex == NULL) {
    screensaverMutex = xSemaphoreCreateMutex();
  }
  
  currentDisplayDriver->initDisplay();
  lastActivityTime = millis();
  isScreensaverActive = false;
}

// Alternate screen state
void alternateScreenState()
{
  if (isScreensaverActive) {
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

// Update activity timestamp on user interaction
void updateActivityTime()
{
  if (screensaverMutex != NULL) {
    xSemaphoreTake(screensaverMutex, portMAX_DELAY);
  }
  
  lastActivityTime = millis();
  
  if (screensaverMutex != NULL) {
    xSemaphoreGive(screensaverMutex);
  }
}

// Check if screensaver timeout has been reached
void checkScreensaver()
{
  // Don't activate if timeout is 0 or negative (disabled)
  if (Settings.ScreensaverTimeout <= 0) {
    return;
  }

  if (screensaverMutex != NULL) {
    xSemaphoreTake(screensaverMutex, portMAX_DELAY);
  }

  // Don't re-activate if already active
  if (isScreensaverActive) {
    if (screensaverMutex != NULL) {
      xSemaphoreGive(screensaverMutex);
    }
    return;
  }

  // Check if timeout exceeded
  // Note: Unsigned arithmetic handles millis() overflow correctly
  unsigned long currentTime = millis();
  // Prevent integer overflow: cap timeout to max safe value (ULONG_MAX / 60000)
  unsigned long timeoutMs = min((unsigned long)Settings.ScreensaverTimeout, ULONG_MAX / 60000UL) * 60000UL;

  if (currentTime - lastActivityTime >= timeoutMs) {
    // Activate screensaver
    lastActiveScreen = currentDisplayDriver->current_cyclic_screen;
    isScreensaverActive = true;
    currentDisplayDriver->alternateScreenState();  // Turn off display
    Serial.printf("Screensaver activated after %d minutes of inactivity\n", Settings.ScreensaverTimeout);
  }

  if (screensaverMutex != NULL) {
    xSemaphoreGive(screensaverMutex);
  }
}

// Getter for screensaver state (thread-safe)
bool getScreensaverActive()
{
  bool active = false;
  if (screensaverMutex != NULL) {
    xSemaphoreTake(screensaverMutex, portMAX_DELAY);
  }
  
  active = isScreensaverActive;
  
  if (screensaverMutex != NULL) {
    xSemaphoreGive(screensaverMutex);
  }
  
  return active;
}

// Wake from screensaver on user activity
void wakeFromScreensaver()
{
  if (screensaverMutex != NULL) {
    xSemaphoreTake(screensaverMutex, portMAX_DELAY);
  }
  
  if (isScreensaverActive) {
    isScreensaverActive = false;
    currentDisplayDriver->alternateScreenState();  // Turn on display
    currentDisplayDriver->current_cyclic_screen = lastActiveScreen;
    Serial.println("Screensaver deactivated - user activity detected");
  }
  lastActivityTime = millis();
  
  if (screensaverMutex != NULL) {
    xSemaphoreGive(screensaverMutex);
  }
}
