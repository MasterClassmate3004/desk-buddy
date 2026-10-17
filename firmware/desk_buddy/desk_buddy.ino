#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>

const int OLED_CLOCK_PIN = 4;
const int OLED_DATA_PIN = 6;
const int OLED_SELECT_PIN = 7;
const int OLED_COMMAND_PIN = 5;
const int OLED_RESET_PIN = 2;

const int START_BUTTON_PIN = 1;
const int PAGE_BUTTON_PIN = 20;
const int MUTE_BUTTON_PIN = 8;
const int BUZZER_PIN = 21;
const int BATTERY_PIN = 0;

const int FOCUS_MINUTES = 25;
const int BREAK_MINUTES = 5;
const float BATTERY_DIVIDER = 2.0;

U8G2_SSD1327_WS_128X128_F_4W_HW_SPI display(
    U8G2_R0, OLED_SELECT_PIN, OLED_COMMAND_PIN, OLED_RESET_PIN);

int currentPage = 0;
bool muted = false;
bool timerRunning = false;
bool timerIsBreak = false;
bool clockIsSet = false;
bool eyesAreClosed = false;

unsigned long timerEndTime = 0;
unsigned long timeLeftWhenPaused = 0;
unsigned long lastClockTick = 0;
unsigned long lastEyeBlink = 0;
unsigned long lastDisplayUpdate = 0;

int clockHours = 0;
int clockMinutes = 0;
int clockSeconds = 0;

void playBeep(int frequency, int duration) {
  if (!muted) {
    tone(BUZZER_PIN, frequency, duration);
  }
}

void waitForButtonRelease(int pin) {
  delay(25);
  while (digitalRead(pin) == LOW) {
    delay(10);
  }
  delay(25);
}

void startOrPauseTimer() {
  if (timerRunning) {
    timeLeftWhenPaused = timerEndTime - millis();
    timerRunning = false;
    playBeep(900, 80);
    return;
  }

  if (timeLeftWhenPaused == 0) {
    int minutes = timerIsBreak ? BREAK_MINUTES : FOCUS_MINUTES;
    timeLeftWhenPaused = (unsigned long)minutes * 60UL * 1000UL;
  }

  timerEndTime = millis() + timeLeftWhenPaused;
  timerRunning = true;
  timeLeftWhenPaused = 0;
  playBeep(1400, 100);
}

void resetTimer() {
  timerRunning = false;
  timeLeftWhenPaused = 0;
  timerIsBreak = false;
  playBeep(700, 100);
}

unsigned long getTimeLeft() {
  if (timerRunning) {
    if ((long)(timerEndTime - millis()) <= 0) {
      return 0;
    }
    return timerEndTime - millis();
  }
  if (timeLeftWhenPaused > 0) {
    return timeLeftWhenPaused;
  }

  int minutes = timerIsBreak ? BREAK_MINUTES : FOCUS_MINUTES;
  return (unsigned long)minutes * 60UL * 1000UL;
}

void updateTimer() {
  if (!timerRunning || getTimeLeft() > 0) {
    return;
  }

  timerRunning = false;
  timeLeftWhenPaused = 0;
  playBeep(1800, 350);
  delay(100);
  playBeep(1800, 350);
  timerIsBreak = !timerIsBreak;
}

void updateClock() {
  if (!clockIsSet || millis() - lastClockTick < 1000) {
    return;
  }

  lastClockTick += 1000;
  clockSeconds++;

  if (clockSeconds >= 60) {
    clockSeconds = 0;
    clockMinutes++;
  }

  if (clockMinutes >= 60) {
    clockMinutes = 0;
    clockHours++;
  }

  if (clockHours >= 24) {
    clockHours = 0;
  }
}

void readClockCommand() {
  if (!Serial.available()) {
    return;
  }

  String command = Serial.readStringUntil('\n');
  command.trim();

  if (!command.startsWith("TIME,")) {
    return;
  }

  int firstComma = command.indexOf(',', 5);
  int secondComma = command.indexOf(',', firstComma + 1);

  if (firstComma < 0 || secondComma < 0) {
    Serial.println("Send time as TIME,hour,minute,second");
    return;
  }

  clockHours = command.substring(5, firstComma).toInt();
  clockMinutes = command.substring(firstComma + 1, secondComma).toInt();
  clockSeconds = command.substring(secondComma + 1).toInt();

  if (clockHours < 0 || clockHours > 23 ||
      clockMinutes < 0 || clockMinutes > 59 ||
      clockSeconds < 0 || clockSeconds > 59) {
    Serial.println("Time values are out of range");
    return;
  }

  clockIsSet = true;
  lastClockTick = millis();
  Serial.println("Clock set");
}

float readBatteryVoltage() {
  float pinVoltage = analogReadMilliVolts(BATTERY_PIN) / 1000.0;
  return pinVoltage * BATTERY_DIVIDER;
}

String twoDigits(int number) {
  if (number < 10) {
    return "0" + String(number);
  }
  return String(number);
}

int getBatteryPercent(float voltage) {
  int percent = (int)((voltage - 3.3) * 100.0 / (4.2 - 3.3));
  return constrain(percent, 0, 100);
}

void drawFace() {
  display.drawRBox(32, eyesAreClosed ? 50 : 42, 23,
                  eyesAreClosed ? 5 : 30, 8);
  display.drawRBox(73, eyesAreClosed ? 50 : 42, 23,
                  eyesAreClosed ? 5 : 30, 8);
  display.drawLine(57, 85, 64, 89);
  display.drawLine(64, 89, 71, 85);
}

void drawClock() {
  display.setFont(u8g2_font_6x12_tf);
  display.drawStr(28, 35, "CLOCK");

  if (!clockIsSet) {
    display.drawStr(15, 68, "Connect Mac to set time");
    display.drawStr(25, 88, "TIME,HH,MM,SS");
    return;
  }

  String timeText = twoDigits(clockHours) + ":" +
                    twoDigits(clockMinutes) + ":" +
                    twoDigits(clockSeconds);
  display.setFont(u8g2_font_logisoso24_tf);
  display.drawStr(12, 85, timeText.c_str());
}

void drawTimer() {
  display.setFont(u8g2_font_6x12_tf);
  display.drawStr(35, 25, timerIsBreak ? "BREAK" : "FOCUS");

  unsigned long secondsLeft = (getTimeLeft() + 999) / 1000;
  String timerText = twoDigits(secondsLeft / 60) + ":" +
                     twoDigits(secondsLeft % 60);

  display.setFont(u8g2_font_logisoso24_tf);
  display.drawStr(27, 78, timerText.c_str());
  display.setFont(u8g2_font_6x12_tf);
  display.drawStr(25, 105, timerRunning ? "Press top button: pause" : "Press top button: start");
}

void drawBattery() {
  float voltage = readBatteryVoltage();
  int percent = getBatteryPercent(voltage);
  String voltageText = String(voltage, 2) + " volts";
  String percentText = String(percent) + "% estimate";

  display.setFont(u8g2_font_6x12_tf);
  display.drawStr(35, 30, "BATTERY");
  display.drawStr(32, 65, voltageText.c_str());
  display.drawStr(38, 88, percentText.c_str());
}

void drawCurrentPage() {
  display.clearBuffer();

  if (currentPage == 0) {
    drawFace();
  } else if (currentPage == 1) {
    drawClock();
  } else if (currentPage == 2) {
    drawTimer();
  } else {
    drawBattery();
  }

  display.sendBuffer();
}

void updateFaceAnimation() {
  if (!eyesAreClosed && millis() - lastEyeBlink > 3000) {
    eyesAreClosed = true;
    lastEyeBlink = millis();
  }

  if (eyesAreClosed && millis() - lastEyeBlink > 150) {
    eyesAreClosed = false;
    lastEyeBlink = millis();
  }
}

void checkButtons() {
  if (digitalRead(START_BUTTON_PIN) == LOW) {
    startOrPauseTimer();
    waitForButtonRelease(START_BUTTON_PIN);
  }

  if (digitalRead(PAGE_BUTTON_PIN) == LOW) {
    currentPage++;
    if (currentPage > 3) {
      currentPage = 0;
    }
    playBeep(1100, 50);
    waitForButtonRelease(PAGE_BUTTON_PIN);
  }

  if (digitalRead(MUTE_BUTTON_PIN) == LOW) {
    muted = !muted;
    if (!muted) {
      playBeep(1200, 80);
    }
    waitForButtonRelease(MUTE_BUTTON_PIN);
  }
}

void setup() {
  pinMode(START_BUTTON_PIN, INPUT_PULLUP);
  pinMode(PAGE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(MUTE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  Serial.begin(115200);
  Serial.setTimeout(30);

  display.begin();
  display.setContrast(180);

  analogReadResolution(12);
  lastClockTick = millis();
}

void loop() {
  readClockCommand();
  checkButtons();
  updateClock();
  updateTimer();
  updateFaceAnimation();

  if (millis() - lastDisplayUpdate > 100) {
    drawCurrentPage();
    lastDisplayUpdate = millis();
  }
}
