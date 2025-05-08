/**
 * @file RFID_Attendance_System.ino
 * @brief Arduino Mega 2560 code for an RFID-based attendance system with timed sessions.
 *
 * This system uses an RFID reader to check authorized cards, displays status on
 * an LCD and a 7-segment display, indicates success/failure with LEDs, and
 * runs a countdown timer for the session duration. Sessions are started via a
 * pushbutton and log data is sent via Serial for processing by a connected device (e.g., Raspberry Pi).
 *
 * @version 1.1
 * @date 2025-05-08
 * @authors Dario Neumann, Jamil Reußwig, Moritz Schulze
 *
 * Components Used:
 * - Arduino Mega 2560
 * - MFRC522 RFID Reader + Cards/Tags
 * - 16x2 I2C LCD Display
 * - 4-Digit 7-Segment Display (Common Cathode)
 * - Green LED
 * - Red LED
 * - Pushbutton
 * - Breadboard & Wires
 *
 * Libraries Required:
 * - LiquidCrystal_I2C (for LCD)
 * - SevSeg (for 7-Segment Display)
 * - SPI (for RFID communication, built-in)
 * - MFRC522 (for RFID Reader)
 */

// ──────────────────────────────────
// Library Includes
// ──────────────────────────────────
#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <SevSeg.h>
#include <SPI.h>
#include <MFRC522.h>

// ──────────────────────────────────
// Pin Definitions
// ──────────────────────────────────
#define GREEN_LED   10
#define RED_LED      9
#define BUTTON_PIN   2

// ──────────────────────────────────
// Hardware Instances
// ──────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 16, 2);
SevSeg sevseg;
const byte numDigits = 4;
byte digitPins[numDigits] = {22, 23, 24, 25};
byte segmentPins[8]   = {26, 27, 28, 29, 30, 31, 32, 33};
#define DISPLAY_TYPE COMMON_CATHODE

// RFID
#define SDA_PIN 53
#define RST_PIN  5
MFRC522 mfrc522(SDA_PIN, RST_PIN);

// ──────────────────────────────────
// Timing & State
// ──────────────────────────────────
unsigned long previousCountdownMillis = 0;
const unsigned long countdownInterval = 1000;
const int  initialCountdownSeconds    = 5700; // 95 min
int  remainingSeconds = initialCountdownSeconds;

unsigned long ledOffMillis     = 0;
unsigned long rfidCooldownMillis = 0;
const unsigned long ledBlinkDuration  = 500;
const unsigned long rfidPauseDuration = 1000;

unsigned long previousRefreshMillis = 0;
const unsigned long refreshInterval = 1;

unsigned long previousRfidCheckMillis = 0;
const unsigned long rfidCheckInterval = 75;

bool greenLedState     = LOW;
bool redLedState       = LOW;
bool rfidReady         = true;
bool countdownStarted  = false;
bool buttonWasPressed  = false;

// ──────────────────────────────────
// Authorised Cards & Session State
// ──────────────────────────────────
const int numCards = 4;
String authorizedUIDs[numCards] = {
  "51 249 205 166",
  "12 34 56 78",
  "19 106 123 19"
};
String names[numCards] = {
  "Alice",
  "Charlie",
  "TestchipJB"
};

// Tracks whether each authorised user is currently *inside* (true) or
// has already logged out (false).  Resets with every new session.
bool insideSession[numCards] = {false};

// ──────────────────────────────────
// Helper Functions
// ──────────────────────────────────
String getNameFromUID(String uid) {
  for (int i = 0; i < numCards; i++) {
    if (uid == authorizedUIDs[i]) return names[i];
  }
  return "Unbekannt";
}

// returns the index of the UID in authorised list, or -1 if unknown.
int getIndexFromUID(String uid) {
  for (int i = 0; i < numCards; i++) {
    if (uid == authorizedUIDs[i]) return i;
  }
  return -1;
}

void resetInsideSession() {
  for (int i = 0; i < numCards; i++) insideSession[i] = false;
}

// ──────────────────────────────────
// SETUP
// ──────────────────────────────────
void setup() {
  Serial.begin(9600);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("System Ready");
  lcd.setCursor(0, 1); lcd.print("Press button...");
  delay(5);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  sevseg.begin(DISPLAY_TYPE, numDigits, digitPins, segmentPins);
  sevseg.setBrightness(20);
  sevseg.setChars("----");

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED,   OUTPUT);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED,   LOW);

  SPI.begin();
  mfrc522.PCD_Init();
}

// ──────────────────────────────────
// LOOP
// ──────────────────────────────────
void loop() {
  unsigned long currentMillis = millis();

  /* ───────── Button Logic ───────── */
  bool buttonIsPressed = (digitalRead(BUTTON_PIN) == LOW);
  if (buttonIsPressed && !buttonWasPressed && !countdownStarted) {
    countdownStarted = true;
    remainingSeconds = initialCountdownSeconds;
    previousCountdownMillis = currentMillis;
    Serial.println("NEW_SESSION");

    resetInsideSession(); 

    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Countdown Laeuft");
    lcd.setCursor(0, 1); lcd.print("           ");
    delay(5);

    int minutes = remainingSeconds / 60;
    int seconds = remainingSeconds % 60;
    sevseg.setNumber(minutes * 100 + seconds, 2);
  }
  buttonWasPressed = buttonIsPressed;

  /* ───────── Countdown ───────── */
  if (countdownStarted && currentMillis - previousCountdownMillis >= countdownInterval && remainingSeconds > 0) {
    previousCountdownMillis = currentMillis;
    remainingSeconds--;
    int minutes = remainingSeconds / 60;
    int seconds = remainingSeconds % 60;
    sevseg.setNumber(minutes * 100 + seconds, 2);
  } else if (countdownStarted && remainingSeconds <= 0) {
    sevseg.setChars("----");
    countdownStarted = false;

    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("System Ready");
    lcd.setCursor(0, 1); lcd.print("Press button...");
    delay(5);
  }

  /* ───────── LED Off Timing ───────── */
  if (greenLedState == HIGH && currentMillis - ledOffMillis >= ledBlinkDuration) {
    greenLedState = LOW; digitalWrite(GREEN_LED, LOW);
  }
  if (redLedState == HIGH && currentMillis - ledOffMillis >= ledBlinkDuration) {
    redLedState = LOW; digitalWrite(RED_LED, LOW);
  }

  /* ───────── RFID Cooldown ───────── */
  if (!rfidReady && currentMillis - rfidCooldownMillis >= rfidPauseDuration) {
    rfidReady = true;
    if (!countdownStarted) {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("System Ready");
      lcd.setCursor(0, 1); lcd.print("Press button...");
      delay(5);
    }
  }

  /* ───────── RFID Reading ───────── */
  if (rfidReady && currentMillis - previousRfidCheckMillis >= rfidCheckInterval) {
    previousRfidCheckMillis = currentMillis;

    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      rfidReady = false; rfidCooldownMillis = currentMillis;

      String uidString = "";
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        uidString += String(mfrc522.uid.uidByte[i], DEC);
        if (i < mfrc522.uid.size - 1) uidString += " ";
      }
      uidString.trim();

      int idx = getIndexFromUID(uidString);

      lcd.clear();
      lcd.setCursor(0, 0);

      if (idx != -1) {
        String name = names[idx];
        if (!insideSession[idx]) {
          /* ─────── Login (first presentation) ─────── */
          insideSession[idx] = true;

          lcd.print("Willkommen");
          lcd.setCursor(0, 1); lcd.print(name);

          digitalWrite(GREEN_LED, HIGH); greenLedState = HIGH; redLedState = LOW; digitalWrite(RED_LED, LOW);
          ledOffMillis = currentMillis;

          Serial.println("LOGIN:" + uidString + "," + name);
        } else {
          /* ─────── Logout (second presentation) ─────── */
          insideSession[idx] = false;

          lcd.print("Auf Wiedersehen");
          lcd.setCursor(0, 1); lcd.print(name);

          digitalWrite(GREEN_LED, HIGH); greenLedState = HIGH; redLedState = LOW; digitalWrite(RED_LED, LOW);
          ledOffMillis = currentMillis;

          Serial.println("LOGOUT:" + uidString + "," + name);
        }
      } else {
        /* ─────── Unknown Card ─────── */
        lcd.print("Karte ungueltig");
        lcd.setCursor(0, 1); lcd.print("           ");

        digitalWrite(RED_LED, HIGH); redLedState = HIGH; greenLedState = LOW; digitalWrite(GREEN_LED, LOW);
        ledOffMillis = currentMillis;
      }
      delay(5);
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
    }
  }

  /* ───────── 7‑Segment Refresh ───────── */
  if (currentMillis - previousRefreshMillis >= refreshInterval) {
    previousRefreshMillis = currentMillis;
    sevseg.refreshDisplay();
  }
}
