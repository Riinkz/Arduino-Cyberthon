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
LiquidCrystal_I2C lcd(0x27, 16, 2); // Initialize LCD object: I2C address 0x27, 16 columns, 2 rows
SevSeg sevseg; // Create SevSeg object
const byte numDigits = 4; // Number of digits on the display
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
unsigned long previousCountdownMillis = 0; // Stores the last time (in ms) the countdown was updated
const unsigned long countdownInterval = 1000; // Decrease countdown every 1000ms
const int  initialCountdownSeconds    = 5700; // 95 min
int  remainingSeconds = initialCountdownSeconds; // Current time remaining

unsigned long ledOffMillis     = 0;
unsigned long rfidCooldownMillis = 0;
const unsigned long ledBlinkDuration  = 500;
const unsigned long rfidPauseDuration = 1000;

unsigned long previousRefreshMillis = 0; // Stores the last time the 7-segment display was refreshed
const unsigned long refreshInterval = 1;

unsigned long previousRfidCheckMillis = 0; // Stores the last time an RFID check was performed
const unsigned long rfidCheckInterval = 75; // How often to check for an RFID card (milliseconds) - Reduces SPI traffic

// Trackers for LED state, TFID state, countdown, button
bool greenLedState     = LOW;
bool redLedState       = LOW;
bool rfidReady         = true;
bool countdownStarted  = false;
bool buttonWasPressed  = false;

// ──────────────────────────────────
// Authorised Cards & Session State
// ──────────────────────────────────
const int numCards = 4; // Number of authorized cards defined
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
/**
 * @brief Checks if a given RFID UID string matches any authorized UID in the list.
 * @param uid The UID string read from the RFID card (space-separated decimal bytes).
 * @return The corresponding name if the UID is found in `authorizedUIDs`, otherwise the string "Unbekannt".
 */
String getNameFromUID(String uid) {
  for (int i = 0; i < numCards; i++) {
    if (uid == authorizedUIDs[i]) return names[i];
  }
  return "Unbekannt";
}

// returns the index of the UID in authorised list, or -1 if unknown.
int getIndexFromUID(String uid) {
  for (int i = 0; i < numCards; i++) { // For each card
    if (uid == authorizedUIDs[i]) return i; // Return the associated name if a match is found
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
  Serial.begin(9600); // Initialize Serial communication (for sending logs and debugging); baud rate of 9600

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("System Ready");
  lcd.setCursor(0, 1); lcd.print("Press button...");
  delay(5); // Small delay after initial LCD write to ensure communication stability

  pinMode(BUTTON_PIN, INPUT_PULLUP);   // Configure the button pin as input with the internal pull-up resistor enabled
                                       // This means the pin reads HIGH when not pressed, LOW when pressed.
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
  unsigned long currentMillis = millis(); // Get the current time in milliseconds since the Arduino started

  /* ───────── Button Logic ───────── */
  bool buttonIsPressed = (digitalRead(BUTTON_PIN) == LOW); // LOW indicated a pressed button due to INPUT_PULLUP

  // Check if the button is pressed NOW but was NOT pressed in the previous loop iteration,
  // AND check if the countdown hasn't already been started. This detects a single press event.
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
  buttonWasPressed = buttonIsPressed;  // Remember the current button state for the next loop to detect changes (edge detection)

  /* ───────── Countdown ───────── */
  // Check if the countdown is active, 1 second has passed, and time is remaining
  if (countdownStarted && currentMillis - previousCountdownMillis >= countdownInterval && remainingSeconds > 0) {
    previousCountdownMillis = currentMillis; // Updating the timestamp
    remainingSeconds--; // Decreasing the remaining time by one second
    int minutes = remainingSeconds / 60;
    int seconds = remainingSeconds % 60;
    sevseg.setNumber(minutes * 100 + seconds, 2); // Format as MMSS integer
  } else if (countdownStarted && remainingSeconds <= 0) { // Check if the countdown was active and has just reached zero or less
    sevseg.setChars("----");
    countdownStarted = false;

    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("System Ready");
    lcd.setCursor(0, 1); lcd.print("Press button...");
    delay(5); // Small delay after LCD update for stability
  }

  /* ───────── LED Off Timing ───────── */
  if (greenLedState == HIGH && currentMillis - ledOffMillis >= ledBlinkDuration) {
    greenLedState = LOW; digitalWrite(GREEN_LED, LOW);
  }
  if (redLedState == HIGH && currentMillis - ledOffMillis >= ledBlinkDuration) {
    redLedState = LOW; digitalWrite(RED_LED, LOW);
  }

  /* ───────── RFID Cooldown ───────── */
  if (!rfidReady && currentMillis - rfidCooldownMillis >= rfidPauseDuration) {  // Checks if the RFID reader is currently in its cooldown period and if enough time has passed
    rfidReady = true;
    if (!countdownStarted) {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("System Ready");
      lcd.setCursor(0, 1); lcd.print("Press button...");
      delay(5);
    }
  }

  /* ───────── RFID Reading ───────── */
  if (rfidReady && currentMillis - previousRfidCheckMillis >= rfidCheckInterval) { // Check if the RFID reader is ready AND if enough time has passed since the last check
    previousRfidCheckMillis = currentMillis;

    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      rfidReady = false; rfidCooldownMillis = currentMillis;

      String uidString = "";
      for (byte i = 0; i < mfrc522.uid.size; i++) { // Loop through the bytes of the UID
        uidString += String(mfrc522.uid.uidByte[i], DEC); // Convert byte to decimal string and append
        if (i < mfrc522.uid.size - 1) uidString += " "; Add a space between bytes for readability
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
