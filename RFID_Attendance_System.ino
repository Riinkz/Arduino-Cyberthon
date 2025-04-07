/**
 * @file RFID_Attendance_System.ino
 * @brief Arduino Mega 2560 code for an RFID-based attendance system with timed sessions.
 *
 * This system uses an RFID reader to check authorized cards, displays status on
 * an LCD and a 7-segment display, indicates success/failure with LEDs, and
 * runs a countdown timer for the session duration. Sessions are started via a
 * pushbutton and log data is sent via Serial for processing by a connected device (e.g., Raspberry Pi).
 *
 * @version 1.0
 * @date 2025-04-07
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

//------------------------------------------------------------------------------
// Library Includes
//------------------------------------------------------------------------------
#include <Arduino.h>          // Core Arduino functions (implicitly included in .ino, good practice)
#include <LiquidCrystal_I2C.h> // For I2C LCD communication
#include <SevSeg.h>           // For controlling the 7-segment display
#include <SPI.h>              // For Serial Peripheral Interface (used by RFID)
#include <MFRC522.h>          // For MFRC522 RFID reader communication

//------------------------------------------------------------------------------
// Pin Definitions
//------------------------------------------------------------------------------
#define GREEN_LED   10        // Digital pin connected to the Green LED (indicates success)
#define RED_LED     9         // Digital pin connected to the Red LED (indicates failure/error)
#define BUTTON_PIN  2         // Digital pin connected to the session start pushbutton

//------------------------------------------------------------------------------
// Hardware Component Setup
//------------------------------------------------------------------------------
// LCD Setup
LiquidCrystal_I2C lcd(0x27, 16, 2);  // Initialize LCD object: I2C address 0x27, 16 columns, 2 rows

// 7-Segment Display Setup
SevSeg sevseg;                      // Create SevSeg object
const byte numDigits = 4;           // Number of digits on the display
byte digitPins[numDigits] = {22, 23, 24, 25}; // Arduino pins connected to the digit common pins (or transistors)
byte segmentPins[8] = {26, 27, 28, 29, 30, 31, 32, 33}; // Arduino pins connected to segments a, b, c, d, e, f, g, dp
#define DISPLAY_TYPE COMMON_CATHODE // Define the type of 7-segment display (COMMON_CATHODE or COMMON_ANODE)

// RFID Setup
#define SDA_PIN 53 // Arduino pin connected to RFID SDA/SS (Slave Select) - Note: Often labeled SS on boards
#define RST_PIN 5  // Arduino pin connected to RFID RST (Reset)
MFRC522 mfrc522(SDA_PIN, RST_PIN); // Create MFRC522 instance

//------------------------------------------------------------------------------
// Timekeeping & Configuration Variables
//------------------------------------------------------------------------------
// Countdown Timer
unsigned long previousCountdownMillis = 0;    // Stores the last time (in ms) the countdown was updated
const unsigned long countdownInterval = 1000; // Interval to decrease countdown (1000ms = 1 second)
const int initialCountdownSeconds = 5700;     // Initial countdown time in seconds (e.g., 5700s = 95 minutes)
int remainingSeconds = initialCountdownSeconds; // Variable holding the current remaining seconds

// Non-blocking Delays / Timers
unsigned long ledOffMillis = 0;           // Stores the time when the active LED should be turned off
unsigned long rfidCooldownMillis = 0;     // Stores the time when the RFID reader becomes ready again after a scan
const unsigned long ledBlinkDuration = 500; // How long the status LED stays lit after a scan (milliseconds)
const unsigned long rfidPauseDuration = 1000; // Cooldown period before reading RFID again (milliseconds)

// Display Refresh Timers
unsigned long previousRefreshMillis = 0;    // Stores the last time the 7-segment display was refreshed
const unsigned long refreshInterval = 1;    // Interval for refreshing the 7-segment display (milliseconds) - Needs to be fast for multiplexing

// RFID Check Timer
unsigned long previousRfidCheckMillis = 0;  // Stores the last time an RFID check was performed
const unsigned long rfidCheckInterval = 75; // How often to check for an RFID card (milliseconds) - Reduces SPI traffic

//------------------------------------------------------------------------------
// State Variables
//------------------------------------------------------------------------------
bool greenLedState = LOW; // Tracks if the green LED is currently supposed to be ON
bool redLedState = LOW;   // Tracks if the red LED is currently supposed to be ON
bool rfidReady = true;    // Tracks if the RFID reader is ready for a new scan (i.e., not in cooldown)
bool countdownStarted = false; // Tracks if the countdown has been started by the button
bool buttonWasPressed = false; // Helps detect a *new* button press (rising/falling edge detection)

//------------------------------------------------------------------------------
// RFID Authorized Data
//------------------------------------------------------------------------------
const int numCards = 4; // Number of authorized cards defined
String authorizedUIDs[numCards] = { // Array storing authorized card UIDs as strings (space-separated decimal bytes)
    "51 249 205 166",
    "131 157 186 6",
    "12 34 56 78",
    "19 106 123 19"
};

String names[numCards] = { // Array storing corresponding names for the authorized UIDs
    "Alice",
    "Bob",
    "Charlie",
    "TestchipJB"
};

//------------------------------------------------------------------------------
// Helper Functions
//------------------------------------------------------------------------------
/**
 * @brief Checks if a given RFID UID string matches any authorized UID in the list.
 * @param uid The UID string read from the RFID card (space-separated decimal bytes).
 * @return The corresponding name if the UID is found in `authorizedUIDs`, otherwise the string "Unbekannt".
 */
String getNameFromUID(String uid) {
    for (int i = 0; i < numCards; i++) { // Loop through all defined authorized cards
        if (uid == authorizedUIDs[i]) { // Compare the scanned UID with the stored UID
            return names[i]; // Return the associated name if a match is found
        }
    }
    return "Unbekannt"; // Return "Unbekannt" (Unknown) if no match is found after checking all UIDs
}

//------------------------------------------------------------------------------
// SETUP FUNCTION - Runs once when the Arduino powers on or resets
//------------------------------------------------------------------------------
void setup() {
    // Initialize Serial communication (for sending logs and debugging)
    Serial.begin(9600); // Start serial communication at 9600 baud rate

    // Initialize LCD Display
    lcd.init();        // Initialize the LCD library
    lcd.backlight();   // Turn on the LCD backlight
    lcd.clear();       // Clear any previous content from the LCD screen
    lcd.setCursor(0, 0); // Set cursor to the first column (0) of the first row (0)
    lcd.print("System Ready"); // Display the initial system status message
    lcd.setCursor(0, 1); // Set cursor to the first column (0) of the second row (1)
    lcd.print("Press button..."); // Prompt the user to start the session
    delay(5);          // Small delay after initial LCD write to ensure communication stability

    // Initialize Button Pin
    pinMode(BUTTON_PIN, INPUT_PULLUP); // Configure the button pin as input with the internal pull-up resistor enabled
                                       // This means the pin reads HIGH when not pressed, LOW when pressed.

    // Initialize 7-Segment Display
    sevseg.begin(DISPLAY_TYPE, numDigits, digitPins, segmentPins); // Initialize the SevSeg library with hardware configuration
    sevseg.setBrightness(20); // Set the display brightness (adjust 0-100 as needed)
    sevseg.setChars("----");  // Display dashes ("----") on the display initially

    // Initialize LED Pins
    pinMode(GREEN_LED, OUTPUT); // Configure the green LED pin as an output
    pinMode(RED_LED, OUTPUT);   // Configure the red LED pin as an output
    digitalWrite(GREEN_LED, LOW); // Ensure the green LED is turned off at the start
    digitalWrite(RED_LED, LOW);   // Ensure the red LED is turned off at the start

    // Initialize SPI Communication Bus (required for the RFID module)
    SPI.begin();

    // Initialize RFID Reader Module
    mfrc522.PCD_Init(); // Initialize the MFRC522 RFID reader module
    // Serial.println("RFID Reader Initialized"); // Optional: Send debug message (commented out to reduce log noise)
}

//------------------------------------------------------------------------------
// LOOP FUNCTION - Runs repeatedly and indefinitely after setup() completes
//------------------------------------------------------------------------------
void loop() {
    // Get the current time in milliseconds since the Arduino started
    unsigned long currentMillis = millis();

    // --- Button Check Logic ---
    // Read the current state of the pushbutton
    bool buttonIsPressed = (digitalRead(BUTTON_PIN) == LOW); // LOW indicates pressed due to INPUT_PULLUP

    // Check if the button is pressed NOW but was NOT pressed in the previous loop iteration,
    // AND check if the countdown hasn't already been started. This detects a single press event.
    if (buttonIsPressed && !buttonWasPressed && !countdownStarted) {
        countdownStarted = true;                      // Set the flag to indicate the countdown is now active
        remainingSeconds = initialCountdownSeconds;   // Reset the timer to its full duration
        previousCountdownMillis = currentMillis;      // Set the starting point for the countdown timer
        // Serial.println("Button pressed, countdown started!"); // Optional debug message
        Serial.println("NEW_SESSION");                // Send signal via Serial to indicate a new session has begun

        // Update LCD to show countdown status
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Countdown Laeuft"); // Display "Countdown Running" (using ae for ä)
        lcd.setCursor(0,1);
        lcd.print("");                 // Clear the second line
        delay(5);                      // Small delay after LCD write for stability

        // Immediately display the starting time on the 7-segment display
        int minutes = remainingSeconds / 60;
        int seconds = remainingSeconds % 60;
        sevseg.setNumber(minutes * 100 + seconds, 2); // Display in MM:SS format (decimal point acts as colon)
    }
    // Remember the current button state for the next loop to detect changes (edge detection)
    buttonWasPressed = buttonIsPressed;


    // --- Countdown Logic (Only executes if countdownStarted is true) ---
    // Check if the countdown is active, 1 second has passed, and time is remaining
    if (countdownStarted && currentMillis - previousCountdownMillis >= countdownInterval && remainingSeconds > 0) {
        previousCountdownMillis = currentMillis; // Update the timestamp for the last second decrement
        remainingSeconds--;                      // Decrease the remaining time by one second

        // Calculate minutes and seconds from remaining total seconds
        int minutes = remainingSeconds / 60;
        int seconds = remainingSeconds % 60;
        int timeNumber = minutes * 100 + seconds; // Format as MMSS integer
        sevseg.setNumber(timeNumber, 2);          // Update the 7-segment display with the new time
    }
    // Check if the countdown was active and has just reached zero or less
    else if (countdownStarted && remainingSeconds <= 0) {
         // --- Timer Finished - Reset System to Initial Idle State ---
         // Serial.println("Countdown finished, returning to idle state."); // Optional debug message
         sevseg.setChars("----");  // Reset the 7-segment display to show dashes
         countdownStarted = false; // Reset the flag, stopping the countdown logic

         // Update the LCD to show the initial "Ready" message and prompt
         lcd.clear();
         lcd.setCursor(0,0);
         lcd.print("System Ready");
         lcd.setCursor(0, 1);
         lcd.print("Press button...");
         delay(5); // Small delay after LCD update for stability
    }

    // --- Non-Blocking LED Control ---
    // Checks if LEDs should be turned off after their blink duration has passed
    // Check Green LED
    if (greenLedState == HIGH && currentMillis - ledOffMillis >= ledBlinkDuration) {
        greenLedState = LOW;          // Update the state variable
        digitalWrite(GREEN_LED, LOW); // Turn off the physical LED
    }
    // Check Red LED
    if (redLedState == HIGH && currentMillis - ledOffMillis >= ledBlinkDuration) {
        redLedState = LOW;        // Update the state variable
        digitalWrite(RED_LED, LOW); // Turn off the physical LED
    }

    // --- Non-Blocking RFID Cooldown ---
    // Checks if the RFID reader is currently in its cooldown period and if enough time has passed
    if (!rfidReady && currentMillis - rfidCooldownMillis >= rfidPauseDuration) {
        rfidReady = true; // Set the flag to indicate the reader is ready for the next scan
        // Serial.println("RFID Reader Ready"); // Optional debug message
        bool lcdUpdated = false; // Track if we changed the LCD in this block

        // Update the LCD to show the ready/prompt message, but ONLY if the countdown isn't active
        if (!countdownStarted) {
            lcd.clear();
            lcd.setCursor(0,0);
            lcd.print("System Ready");
            lcd.setCursor(0, 1);
            lcd.print("Press button...");
            lcdUpdated = true;
        } else {
             // If countdown is running, leave the "Countdown Laeuft" message on the LCD
        }
        // Add a small delay only if the LCD was actually updated in this block
        if (lcdUpdated) delay(5);
    }


    // --- RFID Card Reading Logic (Checked periodically based on rfidCheckInterval) ---
    // Check if the RFID reader is ready AND if enough time has passed since the last check
    if (rfidReady && currentMillis - previousRfidCheckMillis >= rfidCheckInterval) {
        previousRfidCheckMillis = currentMillis; // Update the timestamp of the last check

        // Attempt to detect a new RFID card in proximity
        // mfrc522.PICC_IsNewCardPresent() checks for a card
        // mfrc522.PICC_ReadCardSerial() tries to read the card's serial number (UID)
        if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
            // Card detected and UID read successfully!

            // Immediately start the cooldown period for the reader
            rfidReady = false;
            rfidCooldownMillis = currentMillis;

            // Construct the UID string from the received bytes
            String uidString = "";
            for (byte i = 0; i < mfrc522.uid.size; i++) { // Loop through the bytes of the UID
                uidString += String(mfrc522.uid.uidByte[i], DEC); // Convert byte to decimal string and append
                if (i < mfrc522.uid.size - 1) {
                    uidString += " "; // Add a space between bytes for readability
                }
            }
            uidString.trim(); // Remove any potential leading/trailing whitespace
            // Serial.print("Karte erkannt: "); // Optional debug message
            // Serial.println(uidString); // Optional debug message

            // Look up the name associated with the scanned UID
            String name = getNameFromUID(uidString);

            // --- Update LCD and LEDs based on scan result ---
            lcd.clear(); // Clear the LCD for the new message
            lcd.setCursor(0, 0); // Move cursor to the start of the first line

            if (name != "Unbekannt") {
                // --- Authorized Card ---
                lcd.print("Willkommen"); // Display welcome message
                lcd.setCursor(0, 1);    // Move to the second line
                lcd.print(name);        // Display the authorized user's name

                // Activate Green LED, ensure Red LED is off
                digitalWrite(GREEN_LED, HIGH);
                greenLedState = HIGH; // Update state flag
                redLedState = LOW;
                digitalWrite(RED_LED, LOW);
                ledOffMillis = currentMillis; // Start the timer to turn off the green LED

                // Serial.println(name + " erfolgreich eingecheckt"); // Optional debug message
                // Send formatted log message via Serial for the Raspberry Pi
                Serial.println("LOG:" + uidString + "," + name);
            } else {
                // --- Unauthorized Card ---
                lcd.print("Karte ungueltig"); // Display "Card Invalid" (using ue for German ü)
                lcd.setCursor(0, 1);         // Move to the second line
                lcd.print("");               // Clear the second line

                // Activate Red LED, ensure Green LED is off
                digitalWrite(RED_LED, HIGH);
                redLedState = HIGH; // Update state flag
                greenLedState = LOW;
                digitalWrite(GREEN_LED, LOW);
                ledOffMillis = currentMillis; // Start the timer to turn off the red LED
                // Serial.println("Unbekannte Karte!"); // Optional debug message
            }
            delay(5); // Small delay after LCD write for stability

            // Halt communication with the current card and stop encryption
            // This is important to allow reading the same or other cards later
            mfrc522.PICC_HaltA();
            mfrc522.PCD_StopCrypto1();
        }
        // else: No card was detected during this check interval
    }

    // --- Controlled 7-Segment Display Refresh ---
    // Check if it's time to perform the next step of the multiplexing refresh
    if (currentMillis - previousRefreshMillis >= refreshInterval) {
        previousRefreshMillis = currentMillis; // Update the last refresh time
        sevseg.refreshDisplay(); // Execute the refresh function - crucial for visibility
    }
} // End of loop() function
