#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <SevSeg.h>
#include <SPI.h>
#include <MFRC522.h>

// ---------- Pin Definitions ----------
#define GREEN_LED 10
#define RED_LED   9
#define BUTTON_PIN 2 // Choose an available digital pin for the button

// ---------- LCD setup ----------
LiquidCrystal_I2C lcd(0x27, 16, 2);  // LCD I2C address 0x27, 16 columns x 2 rows

// ---------- 7-Segment Display Setup ----------
SevSeg sevseg;
const byte numDigits = 4;
byte digitPins[numDigits] = {22, 23, 24, 25};
byte segmentPins[8] = {26, 27, 28, 29, 30, 31, 32, 33};
#define DISPLAY_TYPE COMMON_CATHODE

// ---------- RFID Setup ----------
#define SDA 53
#define RST 5
MFRC522 mfrc522(SDA, RST);

// ---------- Timekeeping variables ----------
unsigned long previousCountdownMillis = 0;
const unsigned long countdownInterval = 1000;
const int initialCountdownSeconds = 5700; // 95 minutes in seconds
int remainingSeconds = initialCountdownSeconds;

unsigned long ledOffMillis = 0;
unsigned long rfidCooldownMillis = 0;
const unsigned long ledBlinkDuration = 500;
const unsigned long rfidPauseDuration = 1000;

unsigned long previousRefreshMillis = 0;
const unsigned long refreshInterval = 0;   // Adjustment value to increase SevSeg display readability

unsigned long previousRfidCheckMillis = 0;
const unsigned long rfidCheckInterval = 75;

// ---------- State variables ----------
bool greenLedState = LOW;
bool redLedState = LOW;
bool rfidReady = true;
bool countdownStarted = false;
bool buttonWasPressed = false;

// ---------- RFID authorized UID ----------
const int numCards = 4;
String authorizedUIDs[numCards] = {
    "51 249 205 166",
    "131 157 186 6",
    "12 34 56 78",
    "19 106 123 19"
};

String names[numCards] = {
    "Alice",
    "Bob",
    "Charlie",
    "TestchipJB"
};

// Checking for names belonging to UIDs
String getNameFromUID(String uid) {
    for (int i = 0; i < numCards; i++) {
        if (uid == authorizedUIDs[i]) {
            return names[i];
        }
    }
    return "Unbekannt";
}

void setup() {
    Serial.begin(9600);
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("System Ready");
    lcd.setCursor(0, 1);
    lcd.print("Press button...");
    delay(5); // Small delay after initial LCD write, so nothing breaks

    pinMode(BUTTON_PIN, INPUT_PULLUP);

    sevseg.begin(DISPLAY_TYPE, numDigits, digitPins, segmentPins);
    sevseg.setBrightness(20);
    sevseg.setChars("----");

    pinMode(GREEN_LED, OUTPUT);
    pinMode(RED_LED, OUTPUT);
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED, LOW);

    SPI.begin();
    mfrc522.PCD_Init();
    Serial.println("RFID Reader Initialized");
}

void loop() {
    unsigned long currentMillis = millis();

    // --- Button Check Logic ---
    bool buttonIsPressed = (digitalRead(BUTTON_PIN) == LOW);

    if (buttonIsPressed && !buttonWasPressed && !countdownStarted) {
        countdownStarted = true;
        remainingSeconds = initialCountdownSeconds;
        previousCountdownMillis = currentMillis;
        Serial.println("Button pressed, countdown started!");
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Countdown Laeuft");
        lcd.setCursor(0,1);
        lcd.print("");
        delay(5); // Small delay after LCD write, so nothing breaks

        int minutes = remainingSeconds / 60;
        int seconds = remainingSeconds % 60;
        sevseg.setNumber(minutes * 100 + seconds, 2);
    }
    buttonWasPressed = buttonIsPressed;


    // --- Countdown Logic (Only runs if started) ---
    if (countdownStarted && currentMillis - previousCountdownMillis >= countdownInterval && remainingSeconds > 0) {
        previousCountdownMillis = currentMillis;
        remainingSeconds--;

        int minutes = remainingSeconds / 60;
        int seconds = remainingSeconds % 60;
        int timeNumber = minutes * 100 + seconds;
        sevseg.setNumber(timeNumber, 2);
    } else if (countdownStarted && remainingSeconds <= 0) {
         sevseg.setNumber(0, 2);
    }

    // --- Non-Blocking LED Control ---
    if (greenLedState == HIGH && currentMillis - ledOffMillis >= ledBlinkDuration) {
        greenLedState = LOW;
        digitalWrite(GREEN_LED, LOW);
    }
    if (redLedState == HIGH && currentMillis - ledOffMillis >= ledBlinkDuration) {
        redLedState = LOW;
        digitalWrite(RED_LED, LOW);
    }

    // --- Non-Blocking RFID Cooldown ---
    if (!rfidReady && currentMillis - rfidCooldownMillis >= rfidPauseDuration) {
        rfidReady = true;
        Serial.println("RFID Reader Ready");
        bool lcdUpdated = false; // Flag to check if we updated LCD here
        if (!countdownStarted) {
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("System Ready");
            lcd.setCursor(0, 1);
            lcd.print("Press button...");
            lcdUpdated = true;
        } else {
             // Avoid clearing "Countdown Laeuft" if countdown is active
             // Maybe briefly show "Ready" on line 2?
             // lcd.setCursor(0,1);
             // lcd.print("Ready           "); // Pad with spaces
             // For now, do nothing to avoid overwriting countdown status
        }
        if (lcdUpdated) delay(5); // *** Small delay only if LCD was written to ***
    }

    // --- RFID Card Reading Logic (Checked periodically) ---
    if (rfidReady && currentMillis - previousRfidCheckMillis >= rfidCheckInterval) {
        previousRfidCheckMillis = currentMillis;

        if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
            rfidReady = false;
            rfidCooldownMillis = currentMillis;

            String uidString = "";
            //  UID reading logic
             for (byte i = 0; i < mfrc522.uid.size; i++) {
                uidString += String(mfrc522.uid.uidByte[i], DEC);
                if (i < mfrc522.uid.size - 1) {
                    uidString += " ";
                }
            }
            uidString.trim();
            Serial.print("Karte erkannt: ");
            Serial.println(uidString);

            String name = getNameFromUID(uidString);

            // --- LCD Update on Scan ---
            lcd.clear();
            lcd.setCursor(0, 0);

            if (name != "Unbekannt") {
                lcd.print("Willkommen");
                lcd.setCursor(0, 1);
                lcd.print(name);
                // Green LED logic
                 digitalWrite(GREEN_LED, HIGH);
                 greenLedState = HIGH;
                 redLedState = LOW;
                 digitalWrite(RED_LED, LOW);
                 ledOffMillis = currentMillis;
                 Serial.println(name + " erfolgreich eingecheckt");
            } else {
                lcd.print("Karte ungueltig");
                lcd.setCursor(0, 1);
                lcd.print("");
                // Red LED logic
                 digitalWrite(RED_LED, HIGH);
                 redLedState = HIGH;
                 greenLedState = LOW;
                 digitalWrite(GREEN_LED, LOW);
                 ledOffMillis = currentMillis;
                 Serial.println("Unbekannte Karte!");
            }
            delay(5); // Small delay after LCD write 

            mfrc522.PICC_HaltA();
            mfrc522.PCD_StopCrypto1();
        }
    }

    // --- Controlled SevSeg Refresh ---
    if (currentMillis - previousRefreshMillis >= refreshInterval) {
        previousRefreshMillis = currentMillis;
        sevseg.refreshDisplay();
    }
}
