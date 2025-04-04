#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <SevSeg.h>
#include <SPI.h>
#include <MFRC522.h>

// ---------- LCD setup ----------
LiquidCrystal_I2C lcd(0x27, 16, 2);  // LCD I2C address 0x27, 16 columns x 2 rows

// ---------- 7-Segment Display Setup ----------
SevSeg sevseg;
const byte numDigits = 4;
byte digitPins[numDigits] = {22, 23, 24, 25};
byte segmentPins[8] = {26, 27, 28, 29, 30, 31, 32, 33};
#define DISPLAY_TYPE COMMON_CATHODE // Or COMMON_ANODE depending on your display

// ---------- RFID Setup ----------
#define SDA 53
#define RST 5
MFRC522 mfrc522(SDA, RST);

// ---------- LED Setup ----------
#define GREEN_LED 10
#define RED_LED   9

// ---------- Timekeeping variables ----------
unsigned long previousCountdownMillis = 0;
const unsigned long countdownInterval = 1000; // update countdown value every 1 second
int remainingSeconds = 5400; // 90 minutes in seconds

unsigned long ledOffMillis = 0;
unsigned long rfidCooldownMillis = 0;
const unsigned long ledBlinkDuration = 200;
const unsigned long rfidPauseDuration = 1000;

unsigned long previousRefreshMillis = 0;   // Timer for sevseg refresh
const unsigned long refreshInterval = 1;   // *** TRY REFRESHING EVERY 4ms ***

unsigned long previousRfidCheckMillis = 0; // Timer for RFID check frequency
const unsigned long rfidCheckInterval = 75; // Check for RFID card every 75ms

// ---------- State variables ----------
bool greenLedState = LOW;
bool redLedState = LOW;
bool rfidReady = true;

// ---------- RFID authorized UID ----------
const int numCards = 4; // Number of saved cards
String authorizedUIDs[numCards] = {
    "51 249 205 166",
    "131 157 186 6",
    "12 34 56 78",
    "99 88 77 66"
};

String names[numCards] = {
    "Alice",
    "Bob",
    "Charlie",
    "Emma"
};

// Funktion zur Namenssuche
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

    sevseg.begin(DISPLAY_TYPE, numDigits, digitPins, segmentPins);
    sevseg.setBrightness(20); // *** INCREASE BRIGHTNESS SLIGHTLY ***

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

    // --- Countdown Logic ---
    if (currentMillis - previousCountdownMillis >= countdownInterval && remainingSeconds > 0) {
        previousCountdownMillis = currentMillis;
        remainingSeconds--;

        int minutes = remainingSeconds / 60;
        int seconds = remainingSeconds % 60;
        int timeNumber = minutes * 100 + seconds;
        sevseg.setNumber(timeNumber, 2);
    } else if (remainingSeconds <= 0) {
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
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("System Ready");
    }

    // --- RFID Card Reading Logic (Checked periodically) ---
    if (rfidReady && currentMillis - previousRfidCheckMillis >= rfidCheckInterval) {
        previousRfidCheckMillis = currentMillis;

        if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
            rfidReady = false;
            rfidCooldownMillis = currentMillis;

            String uidString = "";
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

            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Willkommen");
            lcd.setCursor(0, 1);
            lcd.print(name);

            if (name != "Unbekannt") {
                digitalWrite(GREEN_LED, HIGH);
                greenLedState = HIGH;
                redLedState = LOW;
                digitalWrite(RED_LED, LOW);
                ledOffMillis = currentMillis;
                Serial.println("Zugriff erlaubt für: " + name);
            } else {
                digitalWrite(RED_LED, HIGH);
                redLedState = HIGH;
                greenLedState = LOW;
                digitalWrite(GREEN_LED, LOW);
                ledOffMillis = currentMillis;
                Serial.println("Unbekannte Karte!");
            }

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