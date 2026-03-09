// SPDX-FileCopyrightText: 2026 Henry George Grantham-Smith and Alec Parkin
// SPDX-License-Identifier: AGPL-3.0-only

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ========================= PINS =========================

// === Button Pins ===
const int MODE_RIGHT_PIN = 2; // Mode +
const int MODE_LEFT_PIN  = 3; // Mode -
const int ENTER_PIN      = 4; // Hold / Confirm
const int EXIT_PIN       = 5; // Back / Toggle buzzer
const int TARE_PIN       = 6; // Tare
const int OPTION_PIN     = 7; // Extra option (unused)

// === Buzzer Pin ===
const int BUZZER_PIN     = 9;

// === Measurement Pins ===
const int VOLT_PIN       = A0;
const int CURRENT_PIN    = A1;

// ========================= HARDWARE CONSTANTS =========================

// Voltage divider for DC voltage (as in your fixed circuit)
const float R_HIGH = 100000.0f;
const float R_LOW  = 4700.0f;

// Shunt resistor for current (Ohms)
const float Rshunt = 0.1f;

// ========================= UI / STATE =========================

int mode = 0;
const int NUM_MODES = 6;

bool holdMode = false;
bool continuityBuzzerEnabled = true;

// Tare offsets (stored from RAW readings)
float tareVoltage  = 0.0f;
float tareCurrent  = 0.0f;

// Min / Max storage
float minReading = 999999.0f;
float maxReading = -999999.0f;

// Debounce (global cooldown)
unsigned long lastDebounce = 0;
const unsigned long DEBOUNCE_DELAY = 200;

// HOLD snapshot (so HOLD actually holds)
float holdValue = 0.0f;
bool  holdValueValid = false;

// ========================= OLED =========================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ========================= HELPERS =========================

float maybeHold(float fresh) {
  if (!holdMode) {
    holdValueValid = false;
    return fresh;
  }
  if (!holdValueValid) {
    holdValue = fresh;
    holdValueValid = true;
  }
  return holdValue;
}

void showHoldTag() {
  if (holdMode) {
    display.setTextSize(1);
    display.setCursor(96, 0);
    display.println("HOLD");
  }
}

void resetMinMax() {
  minReading = 999999.0f;
  maxReading = -999999.0f;
}

void showMinMax(float value) {
  // Freeze min/max updates while HOLD is active
  if (!holdMode) {
    if (value < minReading) minReading = value;
    if (value > maxReading) maxReading = value;
  }

  display.setTextSize(1);
  display.setCursor(0, 50);
  display.print("MIN:");
  display.print(minReading, 3);

  display.setCursor(70, 50);
  display.print("MAX:");
  display.print(maxReading, 3);
}

// ========================= RAW MEASUREMENTS (NO TARE) =========================

float measureVoltageRaw() {
  // Assumes DEFAULT reference (5V) for your scaling below
  analogReference(DEFAULT);
  int raw = analogRead(VOLT_PIN);
  float v = (raw * (5.0f / 1023.0f)) * ((R_HIGH + R_LOW) / R_LOW);
  return v;
}

float measureVoltage() {
  return measureVoltageRaw() - tareVoltage;
}

float measureCurrentRaw() {
  analogReference(DEFAULT);
  int raw = analogRead(CURRENT_PIN);
  float vShunt = raw * (5.0f / 1023.0f);
  return (vShunt / Rshunt);
}

float measureCurrent() {
  return measureCurrentRaw() - tareCurrent;
}

void performTare() {
  // Store tare from RAW readings only (prevents “double-tare drift”)
  switch (mode) {
    case 0: tareVoltage = measureVoltageRaw(); break;
    case 5: tareCurrent = measureCurrentRaw(); break;
    default: break;
  }
}

// ========================= SETUP / LOOP =========================

void setup() {
  Serial.begin(115200);

  pinMode(MODE_RIGHT_PIN, INPUT_PULLUP);
  pinMode(MODE_LEFT_PIN,  INPUT_PULLUP);
  pinMode(ENTER_PIN,      INPUT_PULLUP);
  pinMode(EXIT_PIN,       INPUT_PULLUP);
  pinMode(TARE_PIN,       INPUT_PULLUP);
  pinMode(OPTION_PIN,     INPUT_PULLUP);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // OLED init
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();
}

void loop() {
  handleButtons();

  display.clearDisplay();
  display.setTextColor(WHITE);

  switch (mode) {
    case 0: showVoltage();     break;
    case 1: showResistance();  break;
    case 2: showCapacitance(); break;
    case 3: showFrequency();   break;
    case 4: showContinuity();  break;
    case 5: showCurrent();     break;
    default: mode = 0; break;
  }

  display.display();
  if (!holdMode) delay(80);
}

// ========================= BUTTON LOGIC =========================

void handleButtons() {
  if (millis() - lastDebounce < DEBOUNCE_DELAY) return;

  if (digitalRead(MODE_RIGHT_PIN) == LOW) {
    noTone(BUZZER_PIN);
    mode = (mode + 1) % NUM_MODES;
    resetMinMax();
    holdValueValid = false;
    lastDebounce = millis();
    return;
  }

  if (digitalRead(MODE_LEFT_PIN) == LOW) {
    noTone(BUZZER_PIN);
    mode = (mode - 1 + NUM_MODES) % NUM_MODES;
    resetMinMax();
    holdValueValid = false;
    lastDebounce = millis();
    return;
  }

  if (digitalRead(TARE_PIN) == LOW) {
    performTare();
    lastDebounce = millis();
    return;
  }

  if (digitalRead(EXIT_PIN) == LOW) {
    continuityBuzzerEnabled = !continuityBuzzerEnabled;
    if (!continuityBuzzerEnabled) noTone(BUZZER_PIN); // silence immediately
    lastDebounce = millis();
    return;
  }

  if (digitalRead(ENTER_PIN) == LOW) {
    holdMode = !holdMode;
    holdValueValid = false; // force new snapshot next draw
    lastDebounce = millis();
    return;
  }
}

// ========================= MODES =========================

void showVoltage() {
  float v = maybeHold(measureVoltage());

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Voltage (V)");

  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print(v, 2);
  display.println(" V");

  showMinMax(v);
  showHoldTag();
}

void showCurrent() {
  float I = maybeHold(measureCurrent());

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Current (A)");

  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print(I, 3);
  display.println(" A");

  showMinMax(I);
  showHoldTag();
}

void showResistance() {
  // NOTE: With your fixed front-end topology, this won’t be a physically “clean” ohmmeter.
  // Code-level hardening: avoid divide-by-zero and don’t hang.

  // Attempt a reading (original approach preserved, but guarded)
  pinMode(VOLT_PIN, OUTPUT);
  digitalWrite(VOLT_PIN, HIGH);
  delay(10);
  pinMode(VOLT_PIN, INPUT);

  int raw = analogRead(VOLT_PIN);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Resistance (Ohm)");

  if (raw <= 1) {
    display.setTextSize(2);
    display.setCursor(0, 20);
    display.println("OPEN");
    showHoldTag();
    return;
  }

  float R = R_HIGH * ((1023.0f / raw) - 1.0f);
  R = maybeHold(R);

  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print(R, 1);
  display.println(" ohm");

  showMinMax(R);
  showHoldTag();
}

void showCapacitance() {
  // Cap across external +Vin and Iin:
  // A0 sees a scaled version of the cap voltage via the fixed divider,
  // so we measure a *ratio* (half of starting ADC) -> divider scale cancels.

  const float R_EQ = (R_HIGH + R_LOW);          // approx discharge path
  const unsigned long TIMEOUT_US = 3000000UL;   // 3 seconds max

#if defined(__AVR__)
  // Better ADC resolution for small A0 voltages on AVR boards (Uno/Nano/etc)
  analogReference(INTERNAL); // 1.1V
  delay(5);
#else
  analogReference(DEFAULT);
#endif

  // If HOLD snapshot already exists, just show it
  if (holdMode && holdValueValid) {
    float C_uF = holdValue;

    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Capacitance (uF)");

    display.setTextSize(2);
    display.setCursor(0, 20);
    display.print(C_uF, 2);
    display.println(" uF");

    showMinMax(C_uF);
    showHoldTag();

#if defined(__AVR__)
    analogReference(DEFAULT);
    delay(5);
#endif
    return;
  }

  // 1) Discharge
  pinMode(VOLT_PIN, OUTPUT);
  digitalWrite(VOLT_PIN, LOW);
  delay(200);

  // 2) Charge (through your 100k path)
  digitalWrite(VOLT_PIN, HIGH);
  delay(200);

  // 3) Float and time decay
  pinMode(VOLT_PIN, INPUT);
  delayMicroseconds(200);

  int adc0 = analogRead(VOLT_PIN);
  if (adc0 < 5) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Capacitance (uF)");

    display.setCursor(0, 20);
    display.println("NO SIGNAL");

#if defined(__AVR__)
    analogReference(DEFAULT);
    delay(5);
#endif
    return;
  }

  int target = adc0 / 2;
  if (target < 2) target = 2;

  unsigned long start = micros();
  while (analogRead(VOLT_PIN) > target) {
    if (micros() - start > TIMEOUT_US) {
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("Capacitance (uF)");

      display.setCursor(0, 20);
      display.println("TIMEOUT");

#if defined(__AVR__)
      analogReference(DEFAULT);
      delay(5);
#endif
      return;
    }
  }
  unsigned long t_us = micros() - start;

  // t = R*C*ln(2) for a half-drop
  float t_s = t_us * 1e-6f;
  float C_F  = t_s / (R_EQ * 0.693147f);
  float C_uF = C_F * 1e6f;

  C_uF = maybeHold(C_uF);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Capacitance (uF)");

  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print(C_uF, 2);
  display.println(" uF");

  showMinMax(C_uF);
  showHoldTag();

#if defined(__AVR__)
  analogReference(DEFAULT);
  delay(5);
#endif
}

void showFrequency() {
  // Dynamic thresholding (min/max + hysteresis) so it works with scaled A0 signals

  if (holdMode && holdValueValid) {
    float freq = holdValue;

    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Frequency (Hz)");

    display.setTextSize(2);
    display.setCursor(0, 20);
    display.print(freq, 1);
    display.println(" Hz");

    showMinMax(freq);
    showHoldTag();
    return;
  }

  const unsigned long CAL_US  = 20000UL;  // 20ms: learn min/max
  const unsigned long MEAS_US = 80000UL;  // 80ms: count edges

  int mn = 1023, mx = 0;
  unsigned long t0 = micros();
  while (micros() - t0 < CAL_US) {
    int v = analogRead(VOLT_PIN);
    if (v < mn) mn = v;
    if (v > mx) mx = v;
  }

  int span = mx - mn;
  if (span < 10) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Frequency (Hz)");

    display.setCursor(0, 20);
    display.println("NO SIGNAL");
    return;
  }

  int mid  = (mn + mx) / 2;
  int hyst = max(2, span / 8);
  int thHi = mid + hyst;
  int thLo = mid - hyst;

  bool lowState = (analogRead(VOLT_PIN) < thLo);
  int risingEdges = 0;

  unsigned long start = micros();
  while (micros() - start < MEAS_US) {
    int v = analogRead(VOLT_PIN);
    if (lowState) {
      if (v > thHi) {
        risingEdges++;
        lowState = false;
      }
    } else {
      if (v < thLo) {
        lowState = true;
      }
    }
  }

  float window_s = MEAS_US * 1e-6f;
  float freq = risingEdges / window_s;

  freq = maybeHold(freq);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Frequency (Hz)");

  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print(freq, 1);
  display.println(" Hz");

  showMinMax(freq);
  showHoldTag();
}

void showContinuity() {
  // Code-level improvements: hysteresis + only tone on state changes + silence when disabled.
  static bool lastCont = false;

  int raw = analogRead(VOLT_PIN);

  // Hysteresis thresholds (tune as needed)
  const int ON_TH  = 40;
  const int OFF_TH = 60;

  bool cont = lastCont ? (raw < OFF_TH) : (raw < ON_TH);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Continuity");

  display.setTextSize(2);
  display.setCursor(0, 20);
  display.println(cont ? "YES" : "NO");

  if (!continuityBuzzerEnabled) {
    noTone(BUZZER_PIN);
  } else if (cont != lastCont) {
    if (cont) tone(BUZZER_PIN, 1000);
    else noTone(BUZZER_PIN);
  }

  lastCont = cont;

  float val = cont ? 0.0f : (float)raw;
  val = maybeHold(val);

  showMinMax(val);
  showHoldTag();
}

