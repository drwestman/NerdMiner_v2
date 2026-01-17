#ifndef DISPLAY_H
#define DISPLAY_H

#include "displayDriver.h"

extern DisplayDriver *currentDisplayDriver;

// Screensaver state
extern bool isScreensaverActive;

void initDisplay();
void alternateScreenState();
void alternateScreenRotation();
void switchToNextScreen();
void switchToPreviousScreen();
void resetToFirstScreen();
void drawLoadingScreen();
void drawSetupScreen();
void drawCurrentScreen(unsigned long mElapsed);
void animateCurrentScreen(unsigned long frame);
void doLedStuff(unsigned long frame);
void updateActivityTime();
void checkScreensaver();
void wakeFromScreensaver();
bool getScreensaverActive();

#endif // DISPLAY_H
