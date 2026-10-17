#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

namespace Pins {
constexpr uint8_t OLED_SCK = 4;
constexpr uint8_t OLED_MOSI = 6;
constexpr uint8_t OLED_CS = 7;
constexpr uint8_t OLED_DC = 5;
constexpr uint8_t OLED_RESET = 2;

constexpr uint8_t BUTTON_START = 1;
constexpr uint8_t BUTTON_PAGE = 20;
constexpr uint8_t BUTTON_MUTE = 8;

constexpr uint8_t BUZZER = 21;
constexpr uint8_t BATTERY_ADC = 0;
}

U8G2_SSD1327_WS_128X128_F_4W_HW_SPI display(U8G2_R0, Pins::OLED_CS,
                                            Pins::OLED_DC, Pins::OLED_RESET);

Preferences preferences;

namespace Config {
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint32_t SERIAL_LINE_TIMEOUT_MS = 80;
constexpr uint16_t DEFAULT_FOCUS_MINUTES = 25;
constexpr uint16_t DEFAULT_BREAK_MINUTES = 5;
constexpr uint16_t MIN_TIMER_MINUTES = 1;
constexpr uint16_t MAX_TIMER_MINUTES = 180;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 28;
constexpr uint32_t LONG_PRESS_MS = 850;
constexpr uint32_t DISPLAY_FRAME_MS = 80;
constexpr uint32_t EYE_BLINK_MIN_MS = 2500;
constexpr uint32_t EYE_BLINK_JITTER_MS = 2800;
constexpr uint32_t BATTERY_SAMPLE_MS = 1200;
constexpr float BATTERY_DIVIDER_RATIO = 2.0f;
constexpr float BATTERY_EMPTY_V = 3.30f;
constexpr float BATTERY_FULL_V = 4.20f;
constexpr uint8_t SERIAL_BUFFER_SIZE = 128;
constexpr uint8_t MAX_SERIAL_ARGS = 4;
}

enum class UiPage : uint8_t { FACE, CLOCK, TIMER, STATS, BATTERY, COUNT };

enum class TimerPhase : uint8_t { FOCUS, BREAK };

enum class TimerState : uint8_t { IDLE, RUNNING, PAUSED, COMPLETE };

struct PersistentSettings {
  uint16_t focusMinutes = Config::DEFAULT_FOCUS_MINUTES;
  uint16_t breakMinutes = Config::DEFAULT_BREAK_MINUTES;
  bool muted = false;
  uint32_t completedFocusSessions = 0;
  uint32_t totalFocusSeconds = 0;
};

struct BatteryReading {
  float volts = 0.0f;
  uint8_t percent = 0;
  bool valid = false;
  bool low = false;
};

struct ButtonState {
  ButtonState() = default;
  explicit ButtonState(uint8_t buttonPin) : pin(buttonPin) {}

  uint8_t pin = 0;
  bool stableLevel = HIGH;
  bool lastRawLevel = HIGH;
  bool longPressSent = false;
  uint32_t rawChangedAt = 0;
  uint32_t pressedAt = 0;
  bool wentDown = false;
  bool wentUp = false;
  bool longPress = false;
};

struct BeepPattern {
  const uint16_t *segments = nullptr;
  uint8_t segmentCount = 0;
  uint8_t currentSegment = 0;
  uint32_t segmentStartedAt = 0;
  bool active = false;
};

PersistentSettings settings;
BatteryReading battery;
ButtonState buttonStart{Pins::BUTTON_START};
ButtonState buttonPage{Pins::BUTTON_PAGE};
ButtonState buttonMute{Pins::BUTTON_MUTE};
BeepPattern beepPattern;

UiPage currentPage = UiPage::FACE;
TimerPhase timerPhase = TimerPhase::FOCUS;
TimerState timerState = TimerState::IDLE;

uint32_t timerDeadline = 0;
uint32_t pausedRemainingMs = 0;
uint32_t lastFocusStart = 0;
uint32_t accumulatedFocusMs = 0;
uint32_t clockEpochAtSync = 0;
uint32_t clockMillisAtSync = 0;
bool clockIsValid = false;
bool screenIsBlank = false;

uint32_t lastDisplayFrame = 0;
uint32_t nextBlinkAt = 0;
uint32_t lastBatterySample = 0;
uint32_t lastStatusBroadcast = 0;
uint32_t lastButtonClickAt = 0;
uint8_t batterySampleCount = 0;
float filteredBatteryMv = 0.0f;

char serialLine[Config::SERIAL_BUFFER_SIZE];
uint8_t serialLineLength = 0;
bool serialLineOverflow = false;

uint32_t elapsedSince(uint32_t now, uint32_t then) {
  return static_cast<uint32_t>(now - then);
}

bool deadlineReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

const char *pageName(UiPage page) {
  switch (page) {
  case UiPage::FACE:
    return "FACE";
  case UiPage::CLOCK:
    return "CLOCK";
  case UiPage::TIMER:
    return "TIMER";
  case UiPage::STATS:
    return "STATS";
  case UiPage::BATTERY:
    return "BATTERY";
  default:
    return "UNKNOWN";
  }
}

const char *phaseName(TimerPhase phase) {
  return phase == TimerPhase::FOCUS ? "FOCUS" : "BREAK";
}

const char *timerStateName(TimerState state) {
  switch (state) {
  case TimerState::IDLE:
    return "IDLE";
  case TimerState::RUNNING:
    return "RUNNING";
  case TimerState::PAUSED:
    return "PAUSED";
  case TimerState::COMPLETE:
    return "COMPLETE";
  default:
    return "UNKNOWN";
  }
}

uint16_t clampMinutes(long value) {
  if (value < Config::MIN_TIMER_MINUTES) {
    return Config::MIN_TIMER_MINUTES;
  }
  if (value > Config::MAX_TIMER_MINUTES) {
    return Config::MAX_TIMER_MINUTES;
  }
  return static_cast<uint16_t>(value);
}

void sendReply(const char *message) {
  Serial.print("OK,");
  Serial.println(message);
}

void sendError(const char *message) {
  Serial.print("ERR,");
  Serial.println(message);
}

void loadSettings() {
  preferences.begin("studybuddy", true);
  settings.focusMinutes =
      preferences.getUShort("focus", Config::DEFAULT_FOCUS_MINUTES);
  settings.breakMinutes =
      preferences.getUShort("break", Config::DEFAULT_BREAK_MINUTES);
  settings.muted = preferences.getBool("muted", false);
  settings.completedFocusSessions = preferences.getUInt("sessions", 0);
  settings.totalFocusSeconds = preferences.getUInt("focussec", 0);
  preferences.end();

  settings.focusMinutes = clampMinutes(settings.focusMinutes);
  settings.breakMinutes = clampMinutes(settings.breakMinutes);
}

void saveSettingUShort(const char *key, uint16_t value) {
  preferences.begin("studybuddy", false);
  preferences.putUShort(key, value);
  preferences.end();
}

void saveMuteSetting() {
  preferences.begin("studybuddy", false);
  preferences.putBool("muted", settings.muted);
  preferences.end();
}

void saveStatistics() {
  preferences.begin("studybuddy", false);
  preferences.putUInt("sessions", settings.completedFocusSessions);
  preferences.putUInt("focussec", settings.totalFocusSeconds);
  preferences.end();
}

void clearStatistics() {
  settings.completedFocusSessions = 0;
  settings.totalFocusSeconds = 0;
  saveStatistics();
}

void setClockFromEpoch(uint32_t epochSeconds) {
  clockEpochAtSync = epochSeconds;
  clockMillisAtSync = millis();
  clockIsValid = epochSeconds > 1700000000UL;
}

uint32_t currentEpochSeconds() {
  if (!clockIsValid) {
    return 0;
  }
  return clockEpochAtSync + elapsedSince(millis(), clockMillisAtSync) / 1000UL;
}

bool formatClock(char *output, size_t outputSize, bool withSeconds) {
  if (!clockIsValid || outputSize < 9) {
    snprintf(output, outputSize, "--:--");
    return false;
  }

  const uint32_t epoch = currentEpochSeconds();
  const time_t rawTime = static_cast<time_t>(epoch);
  struct tm timeInfo;
  if (gmtime_r(&rawTime, &timeInfo) == nullptr) {
    snprintf(output, outputSize, "--:--");
    return false;
  }

  if (withSeconds) {
    snprintf(output, outputSize, "%02d:%02d:%02d", timeInfo.tm_hour,
             timeInfo.tm_min, timeInfo.tm_sec);
  } else {
    snprintf(output, outputSize, "%02d:%02d", timeInfo.tm_hour,
             timeInfo.tm_min);
  }
  return true;
}

int16_t timezoneOffsetMinutes = 0;

void setTimezoneOffset(long offsetMinutes) {
  if (offsetMinutes < -720)
    offsetMinutes = -720;
  if (offsetMinutes > 840)
    offsetMinutes = 840;
  timezoneOffsetMinutes = static_cast<int16_t>(offsetMinutes);
}

uint32_t displayEpochSeconds() {
  if (!clockIsValid)
    return 0;
  const int64_t adjusted = static_cast<int64_t>(currentEpochSeconds()) +
                           static_cast<int64_t>(timezoneOffsetMinutes) * 60LL;
  return adjusted < 0 ? 0 : static_cast<uint32_t>(adjusted);
}

bool formatDisplayClock(char *output, size_t outputSize, bool withSeconds) {
  if (!clockIsValid) {
    snprintf(output, outputSize, "--:--");
    return false;
  }
  const time_t rawTime = static_cast<time_t>(displayEpochSeconds());
  struct tm timeInfo;
  if (gmtime_r(&rawTime, &timeInfo) == nullptr) {
    snprintf(output, outputSize, "--:--");
    return false;
  }
  if (withSeconds) {
    snprintf(output, outputSize, "%02d:%02d:%02d", timeInfo.tm_hour,
             timeInfo.tm_min, timeInfo.tm_sec);
  } else {
    snprintf(output, outputSize, "%02d:%02d", timeInfo.tm_hour,
             timeInfo.tm_min);
  }
  return true;
}

const uint16_t clickPattern[] = {24, 60};
const uint16_t startPattern[] = {55, 70, 55, 0};
const uint16_t finishPattern[] = {110, 90, 110, 90, 240, 0};
const uint16_t warningPattern[] = {180, 100, 180, 0};

void stopBuzzer() {
  digitalWrite(Pins::BUZZER, LOW);
  beepPattern.active = false;
}

void playPattern(const uint16_t *segments, uint8_t segmentCount) {
  if (settings.muted || segments == nullptr || segmentCount == 0) {
    stopBuzzer();
    return;
  }
  beepPattern.segments = segments;
  beepPattern.segmentCount = segmentCount;
  beepPattern.currentSegment = 0;
  beepPattern.segmentStartedAt = millis();
  beepPattern.active = true;
  digitalWrite(Pins::BUZZER, HIGH);
}

void updateBuzzer() {
  if (!beepPattern.active)
    return;
  if (settings.muted) {
    stopBuzzer();
    return;
  }

  const uint32_t now = millis();
  const uint16_t duration = beepPattern.segments[beepPattern.currentSegment];
  if (elapsedSince(now, beepPattern.segmentStartedAt) < duration)
    return;

  beepPattern.currentSegment++;
  if (beepPattern.currentSegment >= beepPattern.segmentCount ||
      beepPattern.segments[beepPattern.currentSegment] == 0) {
    stopBuzzer();
    return;
  }

  beepPattern.segmentStartedAt = now;
  const bool nextIsOn = (beepPattern.currentSegment % 2U) == 0;
  digitalWrite(Pins::BUZZER, nextIsOn ? HIGH : LOW);
}

void feedbackClick() {
  if (!settings.muted) {
    playPattern(clickPattern, sizeof(clickPattern) / sizeof(clickPattern[0]));
  }
}

uint32_t configuredDurationMs() {
  const uint16_t minutes = timerPhase == TimerPhase::FOCUS
                               ? settings.focusMinutes
                               : settings.breakMinutes;
  return static_cast<uint32_t>(minutes) * 60UL * 1000UL;
}

uint32_t remainingTimerMs() {
  if (timerState == TimerState::RUNNING) {
    const uint32_t now = millis();
    if (deadlineReached(now, timerDeadline))
      return 0;
    return timerDeadline - now;
  }
  if (timerState == TimerState::PAUSED)
    return pausedRemainingMs;
  if (timerState == TimerState::COMPLETE)
    return 0;
  return configuredDurationMs();
}

void beginTimer() {
  pausedRemainingMs = configuredDurationMs();
  timerDeadline = millis() + pausedRemainingMs;
  timerState = TimerState::RUNNING;
  if (timerPhase == TimerPhase::FOCUS) {
    lastFocusStart = millis();
    accumulatedFocusMs = 0;
  }
  screenIsBlank = false;
  playPattern(startPattern, sizeof(startPattern) / sizeof(startPattern[0]));
}

void pauseTimer() {
  if (timerState != TimerState::RUNNING)
    return;
  const uint32_t now = millis();
  pausedRemainingMs =
      deadlineReached(now, timerDeadline) ? 0 : timerDeadline - now;
  if (timerPhase == TimerPhase::FOCUS) {
    accumulatedFocusMs += elapsedSince(now, lastFocusStart);
  }
  timerState = TimerState::PAUSED;
}

void resumeTimer() {
  if (timerState != TimerState::PAUSED)
    return;
  timerDeadline = millis() + pausedRemainingMs;
  timerState = TimerState::RUNNING;
  if (timerPhase == TimerPhase::FOCUS)
    lastFocusStart = millis();
}

void resetTimer() {
  timerState = TimerState::IDLE;
  pausedRemainingMs = configuredDurationMs();
  accumulatedFocusMs = 0;
  lastFocusStart = 0;
}

void startOrPauseTimer() {
  switch (timerState) {
  case TimerState::IDLE:
    beginTimer();
    break;
  case TimerState::RUNNING:
    pauseTimer();
    break;
  case TimerState::PAUSED:
    resumeTimer();
    break;
  case TimerState::COMPLETE:
    timerPhase =
        timerPhase == TimerPhase::FOCUS ? TimerPhase::BREAK : TimerPhase::FOCUS;
    beginTimer();
    break;
  }
}

void finishTimer() {
  if (timerState != TimerState::RUNNING)
    return;
  if (timerPhase == TimerPhase::FOCUS) {
    const uint32_t elapsedMs =
        accumulatedFocusMs + elapsedSince(millis(), lastFocusStart);
    const uint32_t elapsedSeconds = elapsedMs / 1000UL;
    settings.completedFocusSessions++;
    settings.totalFocusSeconds += elapsedSeconds;
    saveStatistics();
  }
  timerState = TimerState::COMPLETE;
  pausedRemainingMs = 0;
  playPattern(finishPattern, sizeof(finishPattern) / sizeof(finishPattern[0]));
}

void updateTimer() {
  if (timerState == TimerState::RUNNING &&
      deadlineReached(millis(), timerDeadline)) {
    finishTimer();
  }
}

uint32_t remainingSeconds() { return (remainingTimerMs() + 999UL) / 1000UL; }

void formatDuration(char *output, size_t outputSize, uint32_t seconds) {
  const uint32_t minutes = seconds / 60UL;
  const uint32_t secs = seconds % 60UL;
  snprintf(output, outputSize, "%02lu:%02lu",
           static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(secs));
}

void initializeButton(ButtonState &button) {
  pinMode(button.pin, INPUT_PULLUP);
  button.stableLevel = digitalRead(button.pin);
  button.lastRawLevel = button.stableLevel;
  button.rawChangedAt = millis();
}

void updateButton(ButtonState &button) {
  button.wentDown = false;
  button.wentUp = false;
  button.longPress = false;

  const uint32_t now = millis();
  const bool rawLevel = digitalRead(button.pin);
  if (rawLevel != button.lastRawLevel) {
    button.lastRawLevel = rawLevel;
    button.rawChangedAt = now;
  }

  if (rawLevel != button.stableLevel &&
      elapsedSince(now, button.rawChangedAt) >= Config::BUTTON_DEBOUNCE_MS) {
    button.stableLevel = rawLevel;
    if (button.stableLevel == LOW) {
      button.wentDown = true;
      button.pressedAt = now;
      button.longPressSent = false;
    } else {
      button.wentUp = true;
    }
  }

  if (button.stableLevel == LOW && !button.longPressSent &&
      elapsedSince(now, button.pressedAt) >= Config::LONG_PRESS_MS) {
    button.longPress = true;
    button.longPressSent = true;
  }
}

void cyclePage() {
  const uint8_t next = (static_cast<uint8_t>(currentPage) + 1U) %
                       static_cast<uint8_t>(UiPage::COUNT);
  currentPage = static_cast<UiPage>(next);
}

void toggleMute() {
  settings.muted = !settings.muted;
  saveMuteSetting();
  if (settings.muted)
    stopBuzzer();
  else
    playPattern(clickPattern, sizeof(clickPattern) / sizeof(clickPattern[0]));
}

void handleButtonEvents() {
  updateButton(buttonStart);
  updateButton(buttonPage);
  updateButton(buttonMute);

  if (buttonStart.wentUp && !buttonStart.longPressSent) {
    startOrPauseTimer();
    lastButtonClickAt = millis();
    feedbackClick();
  }
  if (buttonStart.longPress) {
    resetTimer();
    feedbackClick();
  }

  if (buttonPage.wentUp && !buttonPage.longPressSent) {
    cyclePage();
    lastButtonClickAt = millis();
    feedbackClick();
  }
  if (buttonPage.longPress) {
    timerPhase =
        timerPhase == TimerPhase::FOCUS ? TimerPhase::BREAK : TimerPhase::FOCUS;
    resetTimer();
    currentPage = UiPage::TIMER;
    feedbackClick();
  }

  if (buttonMute.wentUp && !buttonMute.longPressSent) {
    toggleMute();
  }
  if (buttonMute.longPress) {
    screenIsBlank = !screenIsBlank;
    if (!screenIsBlank)
      feedbackClick();
  }
}

uint8_t estimateBatteryPercent(float volts) {
  if (volts <= Config::BATTERY_EMPTY_V)
    return 0;
  if (volts >= Config::BATTERY_FULL_V)
    return 100;

  const float fraction = (volts - Config::BATTERY_EMPTY_V) /
                         (Config::BATTERY_FULL_V - Config::BATTERY_EMPTY_V);
  return static_cast<uint8_t>(fraction * 100.0f + 0.5f);
}

void sampleBattery() {
  const uint32_t now = millis();
  if (elapsedSince(now, lastBatterySample) < Config::BATTERY_SAMPLE_MS)
    return;
  lastBatterySample = now;

  const uint32_t pinMilliVolts = analogReadMilliVolts(Pins::BATTERY_ADC);
  if (pinMilliVolts == 0 || pinMilliVolts > 2000) {
    battery.valid = false;
    return;
  }

  if (batterySampleCount == 0)
    filteredBatteryMv = pinMilliVolts;
  else
    filteredBatteryMv = filteredBatteryMv * 0.75f + pinMilliVolts * 0.25f;
  if (batterySampleCount < 250)
    batterySampleCount++;

  battery.volts = (filteredBatteryMv / 1000.0f) * Config::BATTERY_DIVIDER_RATIO;
  battery.percent = estimateBatteryPercent(battery.volts);
  battery.valid = true;
  battery.low = battery.volts < 3.55f;
}

bool eyesAreClosed = false;
uint32_t blinkStartedAt = 0;
uint16_t blinkDurationMs = 150;
uint8_t eyeMood = 0;

void scheduleNextBlink() {
  const uint32_t jitter =
      static_cast<uint32_t>(random(0, Config::EYE_BLINK_JITTER_MS));
  nextBlinkAt = millis() + Config::EYE_BLINK_MIN_MS + jitter;
}

void updateBlinkAnimation() {
  const uint32_t now = millis();
  if (!eyesAreClosed && deadlineReached(now, nextBlinkAt)) {
    eyesAreClosed = true;
    blinkStartedAt = now;
  }
  if (eyesAreClosed && elapsedSince(now, blinkStartedAt) >= blinkDurationMs) {
    eyesAreClosed = false;
    scheduleNextBlink();
  }
}

void setEyeMoodFromTimer() {
  if (timerState == TimerState::COMPLETE)
    eyeMood = 2;
  else if (timerState == TimerState::RUNNING && timerPhase == TimerPhase::FOCUS)
    eyeMood = 1;
  else if (timerPhase == TimerPhase::BREAK)
    eyeMood = 3;
  else
    eyeMood = 0;
}

void drawCenteredText(const char *text, int16_t baselineY) {
  const int16_t width = display.getStrWidth(text);
  const int16_t x = (128 - width) / 2;
  display.drawStr(x, baselineY, text);
}

void drawEye(int16_t centerX, int16_t centerY, bool closed, uint8_t mood) {
  if (closed) {
    display.drawLine(centerX - 15, centerY, centerX + 15, centerY);
    display.drawLine(centerX - 12, centerY + 2, centerX + 12, centerY + 2);
    return;
  }

  const int16_t eyeWidth = mood == 1 ? 26 : 30;
  const int16_t eyeHeight = mood == 3 ? 18 : 26;
  const int16_t top = centerY - eyeHeight / 2;
  display.drawRBox(centerX - eyeWidth / 2, top, eyeWidth, eyeHeight,
                   eyeWidth / 2);

  display.setDrawColor(0);
  const int16_t pupilY = centerY + (mood == 1 ? 3 : 0);
  display.drawDisc(centerX, pupilY, 3);
  display.setDrawColor(1);
}

void drawMouth(uint8_t mood) {
  if (mood == 2) {
    display.drawCircle(64, 94, 7);
    return;
  }
  if (mood == 1) {
    display.drawLine(55, 92, 73, 92);
    return;
  }
  if (mood == 3) {
    display.drawLine(56, 93, 64, 97);
    display.drawLine(64, 97, 72, 93);
    return;
  }
  display.drawLine(55, 88, 60, 92);
  display.drawLine(60, 92, 68, 92);
  display.drawLine(68, 92, 73, 88);
}

void drawFacePage() {
  setEyeMoodFromTimer();
  display.setDrawColor(1);
  display.drawRFrame(5, 5, 118, 118, 14);
  drawEye(42, 55, eyesAreClosed, eyeMood);
  drawEye(86, 55, eyesAreClosed, eyeMood);
  drawMouth(eyeMood);

  display.setFont(u8g2_font_6x12_tf);
  const char *label =
      timerState == TimerState::RUNNING ? phaseName(timerPhase) : "READY";
  drawCenteredText(label, 115);
}

void drawClockPage() {
  char clockText[16];
  formatDisplayClock(clockText, sizeof(clockText), true);
  display.setFont(u8g2_font_6x12_tf);
  drawCenteredText("DESK BUDDY", 18);
  display.setFont(u8g2_font_logisoso24_tf);
  drawCenteredText(clockText, 67);
  display.setFont(u8g2_font_6x12_tf);
  if (clockIsValid) {
    drawCenteredText("MAC TIME SYNCED", 93);
  } else {
    drawCenteredText("CONNECT MAC TO SET TIME", 93);
  }
  char batteryText[20];
  if (battery.valid) {
    snprintf(batteryText, sizeof(batteryText), "BAT %u%%",
             static_cast<unsigned>(battery.percent));
  } else {
    snprintf(batteryText, sizeof(batteryText), "BAT --%%");
  }
  drawCenteredText(batteryText, 115);
}

void drawTimerPage() {
  char duration[16];
  formatDuration(duration, sizeof(duration), remainingSeconds());
  display.setFont(u8g2_font_6x12_tf);
  drawCenteredText(phaseName(timerPhase), 22);
  display.setFont(u8g2_font_logisoso24_tf);
  drawCenteredText(duration, 68);
  display.setFont(u8g2_font_6x12_tf);
  drawCenteredText(timerStateName(timerState), 91);

  char configText[30];
  snprintf(configText, sizeof(configText), "F %umin  B %umin",
           settings.focusMinutes, settings.breakMinutes);
  drawCenteredText(configText, 114);
}

void drawStatsPage() {
  const uint32_t totalMinutes = settings.totalFocusSeconds / 60UL;
  char line[32];
  display.setFont(u8g2_font_6x12_tf);
  drawCenteredText("STUDY STATS", 20);
  snprintf(line, sizeof(line), "FOCUS SESSIONS: %lu",
           static_cast<unsigned long>(settings.completedFocusSessions));
  drawCenteredText(line, 52);
  snprintf(line, sizeof(line), "TOTAL FOCUS: %lu MIN",
           static_cast<unsigned long>(totalMinutes));
  drawCenteredText(line, 74);
  drawCenteredText("SAVED IN DEVICE FLASH", 104);
}

void drawBatteryIcon(int16_t x, int16_t y, uint8_t percent) {
  display.drawFrame(x, y, 54, 20);
  display.drawBox(x + 54, y + 6, 4, 8);
  const uint8_t width = static_cast<uint8_t>((percent * 46U) / 100U);
  if (width > 0)
    display.drawBox(x + 3, y + 3, width, 14);
}

void drawBatteryPage() {
  display.setFont(u8g2_font_6x12_tf);
  drawCenteredText("BATTERY", 20);
  if (!battery.valid) {
    drawCenteredText("NO ADC READING", 58);
    drawCenteredText("CHECK DIVIDER WIRING", 82);
    return;
  }

  char voltageText[20];
  snprintf(voltageText, sizeof(voltageText), "%.2f V", battery.volts);
  drawCenteredText(voltageText, 54);
  drawBatteryIcon(35, 67, battery.percent);
  char percentText[20];
  snprintf(percentText, sizeof(percentText), "%u%% ESTIMATE",
           static_cast<unsigned>(battery.percent));
  drawCenteredText(percentText, 103);
  if (battery.low)
    drawCenteredText("LOW BATTERY", 120);
}

void drawScreenBlankMessage() {
  display.setFont(u8g2_font_6x12_tf);
  drawCenteredText("SCREEN OFF", 61);
  drawCenteredText("HOLD BUTTON 3", 80);
}

void renderDisplay() {
  if (screenIsBlank) {
    display.clearBuffer();
    drawScreenBlankMessage();
    display.sendBuffer();
    return;
  }

  display.clearBuffer();
  switch (currentPage) {
  case UiPage::FACE:
    drawFacePage();
    break;
  case UiPage::CLOCK:
    drawClockPage();
    break;
  case UiPage::TIMER:
    drawTimerPage();
    break;
  case UiPage::STATS:
    drawStatsPage();
    break;
  case UiPage::BATTERY:
    drawBatteryPage();
    break;
  default:
    drawFacePage();
    break;
  }
  display.sendBuffer();
}

void updateDisplay() {
  const uint32_t now = millis();
  if (elapsedSince(now, lastDisplayFrame) < Config::DISPLAY_FRAME_MS)
    return;
  lastDisplayFrame = now;
  updateBlinkAnimation();
  renderDisplay();
}

void sendStatus() {
  char clockText[16];
  formatDisplayClock(clockText, sizeof(clockText), true);
  Serial.print("STATUS,");
  Serial.print("1,");
  Serial.print(pageName(currentPage));
  Serial.print(',');
  Serial.print(timerStateName(timerState));
  Serial.print(',');
  Serial.print(phaseName(timerPhase));
  Serial.print(',');
  Serial.print(remainingSeconds());
  Serial.print(',');
  Serial.print(clockText);
  Serial.print(',');
  Serial.print(settings.muted ? 1 : 0);
  Serial.print(',');
  if (battery.valid)
    Serial.print(battery.volts, 2);
  else
    Serial.print("NA");
  Serial.println();
}

void sendStatistics() {
  Serial.print("STATS,");
  Serial.print(settings.completedFocusSessions);
  Serial.print(',');
  Serial.print(settings.totalFocusSeconds);
  Serial.println();
}

void sendHelp() {
  Serial.println("HELP,1");
  Serial.println("TIME,<unix_seconds>[,<offset_minutes>]");
  Serial.println("START | PAUSE | RESET");
  Serial.println("SETFOCUS,<minutes> | SETBREAK,<minutes>");
  Serial.println("MUTE,0|1 | PAGE,FACE|CLOCK|TIMER|STATS|BATTERY");
  Serial.println("STATUS | STATS | CLEARSTATS | HELP");
}

void setPageByName(const char *value) {
  if (strcasecmp(value, "FACE") == 0)
    currentPage = UiPage::FACE;
  else if (strcasecmp(value, "CLOCK") == 0)
    currentPage = UiPage::CLOCK;
  else if (strcasecmp(value, "TIMER") == 0)
    currentPage = UiPage::TIMER;
  else if (strcasecmp(value, "STATS") == 0)
    currentPage = UiPage::STATS;
  else if (strcasecmp(value, "BATTERY") == 0)
    currentPage = UiPage::BATTERY;
  else {
    sendError("BAD_PAGE");
    return;
  }
  sendReply("PAGE_SET");
}

uint8_t splitArguments(char *line, char *args[], uint8_t maxArgs) {
  uint8_t count = 0;
  char *token = strtok(line, ",");
  while (token != nullptr && count < maxArgs) {
    while (*token == ' ' || *token == '\t')
      token++;
    char *end = token + strlen(token);
    while (end > token && (end[-1] == ' ' || end[-1] == '\t')) {
      *--end = '\0';
    }
    args[count++] = token;
    token = strtok(nullptr, ",");
  }
  return count;
}

bool parseLongStrict(const char *text, long &value) {
  if (text == nullptr || *text == '\0')
    return false;
  char *end = nullptr;
  value = strtol(text, &end, 10);
  return end != text && *end == '\0';
}

void handleCommand(char *line) {
  char *args[Config::MAX_SERIAL_ARGS] = {nullptr};
  const uint8_t count = splitArguments(line, args, Config::MAX_SERIAL_ARGS);
  if (count == 0)
    return;

  if (strcasecmp(args[0], "TIME") == 0) {
    long epoch = 0;
    if (count < 2 || !parseLongStrict(args[1], epoch) || epoch <= 0) {
      sendError("TIME_EXPECTS_UNIX_SECONDS");
      return;
    }
    setClockFromEpoch(static_cast<uint32_t>(epoch));
    if (count >= 3) {
      long offset = 0;
      if (!parseLongStrict(args[2], offset)) {
        sendError("BAD_TIMEZONE_OFFSET");
        return;
      }
      setTimezoneOffset(offset);
    }
    sendReply("TIME_SET");
    sendStatus();
    return;
  }

  if (strcasecmp(args[0], "START") == 0) {
    if (timerState == TimerState::PAUSED || timerState == TimerState::IDLE ||
        timerState == TimerState::COMPLETE)
      startOrPauseTimer();
    sendReply("STARTED");
    return;
  }

  if (strcasecmp(args[0], "PAUSE") == 0) {
    pauseTimer();
    sendReply("PAUSED");
    return;
  }

  if (strcasecmp(args[0], "RESET") == 0) {
    resetTimer();
    sendReply("RESET");
    return;
  }

  if (strcasecmp(args[0], "SETFOCUS") == 0 ||
      strcasecmp(args[0], "SETBREAK") == 0) {
    long minutes = 0;
    if (count < 2 || !parseLongStrict(args[1], minutes)) {
      sendError("TIMER_EXPECTS_MINUTES");
      return;
    }
    const uint16_t clamped = clampMinutes(minutes);
    if (strcasecmp(args[0], "SETFOCUS") == 0) {
      settings.focusMinutes = clamped;
      saveSettingUShort("focus", clamped);
    } else {
      settings.breakMinutes = clamped;
      saveSettingUShort("break", clamped);
    }
    if (timerState == TimerState::IDLE) {
      pausedRemainingMs = configuredDurationMs();
    }
    sendReply("TIMER_LENGTH_SET");
    return;
  }

  if (strcasecmp(args[0], "MUTE") == 0) {
    long muteValue = 0;
    if (count < 2 || !parseLongStrict(args[1], muteValue) ||
        (muteValue != 0 && muteValue != 1)) {
      sendError("MUTE_EXPECTS_0_OR_1");
      return;
    }
    settings.muted = muteValue == 1;
    saveMuteSetting();
    if (settings.muted)
      stopBuzzer();
    sendReply("MUTE_SET");
    return;
  }

  if (strcasecmp(args[0], "PAGE") == 0) {
    if (count < 2)
      sendError("PAGE_EXPECTS_NAME");
    else
      setPageByName(args[1]);
    return;
  }

  if (strcasecmp(args[0], "STATUS") == 0) {
    sendStatus();
    return;
  }

  if (strcasecmp(args[0], "STATS") == 0) {
    sendStatistics();
    return;
  }

  if (strcasecmp(args[0], "CLEARSTATS") == 0) {
    clearStatistics();
    sendReply("STATS_CLEARED");
    return;
  }

  if (strcasecmp(args[0], "HELP") == 0) {
    sendHelp();
    return;
  }

  sendError("UNKNOWN_COMMAND");
}

void consumeSerialByte(char incoming) {
  if (incoming == '\r')
    return;
  if (incoming == '\n') {
    if (serialLineOverflow) {
      sendError("LINE_TOO_LONG");
    } else {
      serialLine[serialLineLength] = '\0';
      handleCommand(serialLine);
    }
    serialLineLength = 0;
    serialLineOverflow = false;
    return;
  }

  if (serialLineOverflow)
    return;
  if (serialLineLength + 1 >= Config::SERIAL_BUFFER_SIZE) {
    serialLineOverflow = true;
    return;
  }
  serialLine[serialLineLength++] = incoming;
}

void updateSerialProtocol() {
  while (Serial.available() > 0) {
    consumeSerialByte(static_cast<char>(Serial.read()));
  }
}

void initializeDisplay() {
  SPI.begin(Pins::OLED_SCK, -1, Pins::OLED_MOSI, Pins::OLED_CS);
  display.begin();
  display.setContrast(160);
  display.clearBuffer();
  display.setFont(u8g2_font_6x12_tf);
  drawCenteredText("DESK BUDDY", 54);
  drawCenteredText("STARTING...", 76);
  display.sendBuffer();
}

void initializeBatteryMonitor() {
  analogReadResolution(12);
  analogSetPinAttenuation(Pins::BATTERY_ADC, ADC_11db);
  pinMode(Pins::BATTERY_ADC, INPUT);
}

void initializeBuzzer() {
  pinMode(Pins::BUZZER, OUTPUT);
  digitalWrite(Pins::BUZZER, LOW);
}

void printBootBanner() {
  Serial.println();
  Serial.println("DESK_BUDDY,1");
  Serial.println("READY");
  Serial.println("Send HELP for commands");
}

void setup() {
  Serial.begin(Config::SERIAL_BAUD);
  delay(40);

  initializeBuzzer();
  initializeBatteryMonitor();
  initializeButton(buttonStart);
  initializeButton(buttonPage);
  initializeButton(buttonMute);
  loadSettings();
  initializeDisplay();

  randomSeed(static_cast<uint32_t>(analogRead(Pins::BATTERY_ADC)) ^ micros());
  scheduleNextBlink();
  pausedRemainingMs = configuredDurationMs();
  printBootBanner();
}

void loop() {
  updateSerialProtocol();
  handleButtonEvents();
  updateTimer();
  updateBuzzer();
  sampleBattery();
  updateDisplay();

  if (elapsedSince(millis(), lastStatusBroadcast) >= 1000UL) {
    lastStatusBroadcast = millis();
  }
}
