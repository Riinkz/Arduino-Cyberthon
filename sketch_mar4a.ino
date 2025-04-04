#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <SevSeg.h>
#include <SPI.h>
#include <MFRC522.h>

// ---------- Pin Definitions ----------
#define GREEN_LED 10
#define RED_LED   9
#define BUTTON_PIN 2 // Digital pin for the start button

// ---------- LCD setup ----------
LiquidCrystal_I2C lcd(0x27, 16, 2);  // LCD I2C address 0x27, 16 columns x 2 rows

// ---------- 7-Segment Display Setup ----------
SevSeg sevseg;
const byte numDigits = 4;
byte digitPins[numDigits] = {22, 23, 24, 25}; // Pins for controlling digits
byte segmentPins[8] = {26, 27, 28, 29, 30, 31, 32, 33}; // Pins for controlling segments (a-g, dp)
#define DISPLAY_TYPE COMMON_CATHODE 

// ---------- RFID Setup ----------
#define SDA 53 // Slave Select/Chip Select pin for RFID module
#define RST 5  // Reset pin for RFID module
MFRC522 mfrc522(SDA, RST);

// ---------- Timekeeping variables ----------
unsigned long previousCountdownMillis = 0;    // Stores the last time the countdown was updated
const unsigned long countdownInterval = 1000; // Interval to decrease countdown (1000ms = 1 second)
const int initialCountdownSeconds = 5700;      // Initial countdown time in seconds 
int remainingSeconds = initialCountdownSeconds; // Variable holding the current remaining seconds

unsigned long ledOffMillis = 0;           // Stores the time when the active LED should be turned off
unsigned long rfidCooldownMillis = 0;     // Stores the time when the RFID reader becomes ready again
const unsigned long ledBlinkDuration = 500; // How long the LED stays lit (in milliseconds)
const unsigned long rfidPauseDuration = 1000; // How long to wait before reading RFID again (in milliseconds)

unsigned long previousRefreshMillis = 0;    // Stores the last time the 7-segment display was refreshed
const unsigned long refreshInterval = 1;    // Interval for refreshing the 7-segment display (multiplexing)

unsigned long previousRfidCheckMillis = 0;  // Stores the last time an RFID check was performed
const unsigned long rfidCheckInterval = 75; // How often to check for an RFID card (in milliseconds)

// ---------- State variables ----------
bool greenLedState = LOW; // Tracks if the green LED is currently supposed to be ON
bool redLedState = LOW;   // Tracks if the red LED is currently supposed to be ON
bool rfidReady = true;    // Tracks if the RFID reader is ready for a new scan (not in cooldown)
bool countdownStarted = false; // Tracks if the countdown has been started by the button
bool buttonWasPressed = false; // Helps detect a new button press (edge detection)

// ---------- RFID authorized UID & Names ----------
const int numCards = 4; // Number of authorized cards
String authorizedUIDs[numCards] = { // Array of authorized card UIDs (as strings)
    "51 249 205 166",
    "131 157 186 6",
    "12 34 56 78",
    "19 106 123 19"
};

String names[numCards] = { // Corresponding names for the authorized UIDs
    "Alice",
    "Bob",
    "Charlie",
    "TestchipJB"
};

/**
 * @brief Checks if a given RFID UID string matches any authorized UID.
 * @param uid The UID string read from the RFID card.
 * @return The corresponding name if the UID is authorized, otherwise "Unbekannt".
 */
String getNameFromUID(String uid) {
    for (int i = 0; i < numCards; i++) {
        if (uid == authorizedUIDs[i]) {
            return names[i]; // Return the name if UID matches
        }
    }
    return "Unbekannt"; // Return "Unbekannt" if no match is found
}

//------------------------------------------------------------------------------
// SETUP FUNCTION - Runs once when the Arduino starts
//------------------------------------------------------------------------------
void setup() {
    // Initialize Serial communication for debugging
    Serial.begin(9600);

    // Initialize LCD
    lcd.init();        // Initialize the LCD library
    lcd.backlight();   // Turn on the LCD backlight
    lcd.clear();       // Clear any previous content
    lcd.setCursor(0, 0); // Set cursor to first line, first position
    lcd.print("System Ready"); // Display initial message
    lcd.setCursor(0, 1); // Set cursor to second line, first position
    lcd.print("Press button..."); // Prompt user
    delay(5);          // Small delay after initial LCD write to ensure stability

    // Initialize Button Pin
    pinMode(BUTTON_PIN, INPUT_PULLUP); // Set the button pin as input with internal pull-up resistor

    // Initialize 7-Segment Display
    sevseg.begin(DISPLAY_TYPE, numDigits, digitPins, segmentPins); // Initialize the SevSeg library
    sevseg.setBrightness(20); // Set display brightness (0-100)
    sevseg.setChars("----");  // Display dashes initially before countdown starts

    // Initialize LED Pins
    pinMode(GREEN_LED, OUTPUT); // Set green LED pin as output
    pinMode(RED_LED, OUTPUT);   // Set red LED pin as output
    digitalWrite(GREEN_LED, LOW); // Ensure green LED is off initially
    digitalWrite(RED_LED, LOW);   // Ensure red LED is off initially

    // Initialize SPI Communication (for RFID)
    SPI.begin();

    // Initialize RFID Reader
    mfrc522.PCD_Init(); // Initialize the MFRC522 RFID reader module
    Serial.println("RFID Reader Initialized"); // Print status to Serial Monitor
}

//------------------------------------------------------------------------------
// LOOP FUNCTION - Runs repeatedly after setup()
//------------------------------------------------------------------------------
void loop() {
    unsigned long currentMillis = millis(); // Get the current time (milliseconds since start)

    // --- Button Check Logic ---
    // Check the current physical state of the button
    bool buttonIsPressed = (digitalRead(BUTTON_PIN) == LOW); // LOW means pressed because of INPUT_PULLUP

    // Check for a *new* press (was not pressed last loop, is pressed now)
    // Only start if the countdown hasn't already been started
    if (buttonIsPressed && !buttonWasPressed && !countdownStarted) {
        countdownStarted = true;                      // Set the flag indicating countdown is active
        remainingSeconds = initialCountdownSeconds;   // Reset the countdown timer to its initial value
        previousCountdownMillis = currentMillis;      // Reset the countdown timer baseline
        Serial.println("Button pressed, countdown started!"); // Debug message

        // Update LCD
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Countdown Laeuft"); // Display "Countdown Running" (using ae for ä)
        lcd.setCursor(0,1);
        lcd.print(""); // Clear second line
        delay(5); // Small delay after LCD write for stability

        // Display the initial time on the 7-segment display immediately
        int minutes = remainingSeconds / 60;
        int seconds = remainingSeconds % 60;
        sevseg.setNumber(minutes * 100 + seconds, 2); // Display MM:SS format
    }
    // Remember the button state for the next loop iteration to detect changes
    buttonWasPressed = buttonIsPressed;


    // --- Countdown Logic (Only runs if started) ---
    // Check if the countdown is active and if 1 second has passed
    if (countdownStarted && currentMillis - previousCountdownMillis >= countdownInterval && remainingSeconds > 0) {
        previousCountdownMillis = currentMillis; // Update the last countdown time
        remainingSeconds--;                      // Decrement the remaining seconds

        // Calculate minutes and seconds for display
        int minutes = remainingSeconds / 60;
        int seconds = remainingSeconds % 60;
        int timeNumber = minutes * 100 + seconds; // Combine into MMSS format
        sevseg.setNumber(timeNumber, 2);          // Update the 7-segment display
    }
    // Check if countdown is active and has reached zero
    else if (countdownStarted && remainingSeconds <= 0) {
         // --- Timer Finished - Reset to Initial State ---
         Serial.println("Countdown finished, returning to idle state.");
         sevseg.setChars("----"); // Reset 7-segment display to dashes
         countdownStarted = false; // Reset the countdown flag

         // Update LCD to initial state message
         lcd.clear();
         lcd.setCursor(0,0);
         lcd.print("System Ready");
         lcd.setCursor(0, 1);
         lcd.print("Press button...");
         delay(5); // Small delay after LCD update
    }

    // --- Non-Blocking LED Control ---
    // Check if the green LED is on and its display duration has passed
    if (greenLedState == HIGH && currentMillis - ledOffMillis >= ledBlinkDuration) {
        greenLedState = LOW;          // Update state flag
        digitalWrite(GREEN_LED, LOW); // Turn the LED off
    }
    // Check if the red LED is on and its display duration has passed
    if (redLedState == HIGH && currentMillis - ledOffMillis >= ledBlinkDuration) {
        redLedState = LOW;        // Update state flag
        digitalWrite(RED_LED, LOW); // Turn the LED off
    }

    // --- Non-Blocking RFID Cooldown ---
    // Check if RFID is in cooldown and if the cooldown duration has passed
    if (!rfidReady && currentMillis - rfidCooldownMillis >= rfidPauseDuration) {
        rfidReady = true; // RFID reader is ready again
        Serial.println("RFID Reader Ready");
        bool lcdUpdated = false; // Flag to track if LCD was updated here

        // Update LCD only if countdown is not currently running
        if (!countdownStarted) {
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("System Ready");
            lcd.setCursor(0, 1);
            lcd.print("Press button...");
            lcdUpdated = true;
        } else {
             // If countdown is running, don't overwrite the "Countdown Laeuft" message
             // (Could add a temporary "RFID Ready" message elsewhere if needed)
        }
        // Add small delay only if we actually wrote to the LCD
        if (lcdUpdated) delay(5);
    }


    // --- RFID Card Reading Logic (Checked periodically) ---
    // Check if RFID reader is ready and if it's time for the next periodic check
    if (rfidReady && currentMillis - previousRfidCheckMillis >= rfidCheckInterval) {
        previousRfidCheckMillis = currentMillis; // Update the last check time

        // Attempt to detect a new RFID card
        if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
            // Card detected, start cooldown immediately
            rfidReady = false;
            rfidCooldownMillis = currentMillis;

            // Read the UID into a String
            String uidString = "";
            for (byte i = 0; i < mfrc522.uid.size; i++) {
                uidString += String(mfrc522.uid.uidByte[i], DEC); // Append byte as decimal string
                if (i < mfrc522.uid.size - 1) {
                    uidString += " "; // Add space separator between bytes
                }
            }
            uidString.trim(); // Remove any leading/trailing whitespace
            Serial.print("Karte erkannt: "); // Debug message
            Serial.println(uidString);

            // Check if the UID is authorized
            String name = getNameFromUID(uidString);

            // --- LCD Update on Scan ---
            lcd.clear(); // Clear previous message
            lcd.setCursor(0, 0); // Go to first line

            if (name != "Unbekannt") {
                // Authorized Card Found
                lcd.print("Willkommen"); // Display welcome
                lcd.setCursor(0, 1);    // Go to second line
                lcd.print(name);        // Display name

                // Activate Green LED, deactivate Red LED
                digitalWrite(GREEN_LED, HIGH);
                greenLedState = HIGH;
                redLedState = LOW;
                digitalWrite(RED_LED, LOW);
                ledOffMillis = currentMillis; // Start timer to turn green LED off
                Serial.println(name + " erfolgreich eingecheckt"); // Debug message
            } else {
                // Unauthorized Card Found
                lcd.print("Karte ungueltig"); // Display "Card Invalid" (using ue for ü)
                lcd.setCursor(0, 1);         // Go to second line
                lcd.print("");               // Clear second line

                // Activate Red LED, deactivate Green LED
                digitalWrite(RED_LED, HIGH);
                redLedState = HIGH;
                greenLedState = LOW;
                digitalWrite(GREEN_LED, LOW);
                ledOffMillis = currentMillis; // Start timer to turn red LED off
                Serial.println("Unbekannte Karte!"); // Debug message
            }
            delay(5); // Small delay after LCD write for stability

            // Halt the card and stop encryption to allow reading again later
            mfrc522.PICC_HaltA();
            mfrc522.PCD_StopCrypto1();
        }
        // else: No card detected during this check interval
    }

    // --- Controlled SevSeg Refresh ---
    // Refresh the 7-segment display periodically for multiplexing
    if (currentMillis - previousRefreshMillis >= refreshInterval) {
        previousRefreshMillis = currentMillis;
        sevseg.refreshDisplay(); // This MUST run frequently
    }
}
