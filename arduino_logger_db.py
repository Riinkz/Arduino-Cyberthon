#!/usr/bin/env python3
"""
arduino_logger_db.py
---------------------
Reads specific log data from an Arduino via serial connection,
manages session data in a SQLite database, and prevents duplicate
name entries per session.

Author: Jamil Reußwig 
"""

import serial
import time
import sqlite3
import sys

# --- Configuration ---
# Use command-line argument for serial port if provided, otherwise default
SERIAL_PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
BAUD_RATE = 9600
DATABASE = "arduino_data.db"
LOG_PREFIX = "LOG:"
LOGOUT_PREFIX = "LOGOUT:"
SESSION_SIGNAL = "NEW_SESSION"

def setup_database():
    """Create the database and logs table if they don't exist."""
    conn = sqlite3.connect(DATABASE)
    cursor = conn.cursor()
    # Ensure table exists - structure is ID, Timestamp, Name
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT NOT NULL,
            message TEXT NOT NULL
        )
    ''')
    # Consider adding an index for faster name lookups if logs get large within a session
    # cursor.execute("CREATE INDEX IF NOT EXISTS idx_message ON logs (message)")
    conn.commit()
    print(f"Database '{DATABASE}' initialized.")
    return conn

def clear_logs(conn):
    """Delete all records from the logs table."""
    cursor = conn.cursor()
    try:
        cursor.execute("DELETE FROM logs")
        # Optional: Reset the autoincrement counter if desired (for SQLite)
        # cursor.execute("DELETE FROM sqlite_sequence WHERE name='logs'")
        conn.commit()
        print("Log table cleared for new session.")
    except sqlite3.Error as e:
        print(f"Error clearing logs: {e}")


def check_if_name_exists(conn, name):
    """Check if a name already exists in the logs table for this session."""
    cursor = conn.cursor()
    cursor.execute("SELECT id FROM logs WHERE message = ?", (name,))
    return cursor.fetchone() is not None # Returns True if name exists, False otherwise

def delete_log(conn, name):
    """Delete a log record for a specific name from the database."""
    cursor = conn.cursor()
    try:
        cursor.execute("DELETE FROM logs WHERE message = ?", (name,))
        conn.commit()
        if cursor.rowcount > 0:
            print(f"[{time.strftime('%Y-%m-%d %H:%M:%S', time.localtime())}] Logout: {name} removed from logs")
            return True
        else:
            print(f"[{time.strftime('%Y-%m-%d %H:%M:%S', time.localtime())}] Warning: {name} not found in logs for logout")
            return False
    except sqlite3.Error as e:
        print(f"Error deleting log for {name}: {e}")
        return False

def insert_log(conn, timestamp, name):
    """Insert a log record (Timestamp, Name) into the database."""
    cursor = conn.cursor()
    try:
        cursor.execute("INSERT INTO logs (timestamp, message) VALUES (?, ?)", (timestamp, name))
        conn.commit()
        print(f"[{timestamp}] Logging: {name}")
    except sqlite3.Error as e:
        print(f"Error inserting log for {name}: {e}")


def main():
    # Set up the SQLite database connection
    conn = setup_database()
    if not conn:
        print("Failed to connect to database. Exiting.")
        return

    ser = None # Initialize serial connection variable

    print("Attempting to connect to Arduino...")
    while ser is None: # Keep trying to connect
        try:
            ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
            print(f"Successfully connected to {SERIAL_PORT}")
            print("Waiting for Arduino to initialize...")
            time.sleep(2) # Give time for Arduino bootloader and setup

        except serial.SerialException as e:
            print(f"Error opening serial port {SERIAL_PORT}: {e}")
            print("Will retry in 5 seconds...")
            time.sleep(5)
        except KeyboardInterrupt:
            print("\nExiting during connection attempt.")
            if conn: conn.close()
            return


    print("Listening for data from Arduino...")
    try:
        while True:
            try:
                if not ser.is_open:
                    print("Serial port closed unexpectedly. Attempting to reconnect...")
                    ser.close()
                    time.sleep(5)
                    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
                    print(f"Reconnected to {SERIAL_PORT}")
                    time.sleep(2) # Wait after reconnecting


                line_bytes = ser.readline()
                if not line_bytes:
                    # Timeout occurred, just continue loop
                    continue

                # Decode bytes to string
                line = line_bytes.decode('utf-8', errors='replace').strip()

                if line: # Check if line is not empty after stripping
                    # --- Process Received Line ---
                    current_time = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime())

                    if line == SESSION_SIGNAL:
                        clear_logs(conn)

                    elif line.startswith(LOG_PREFIX):
                        try:
                            # Extract data part, remove prefix
                            data_part = line[len(LOG_PREFIX):]
                            # Split by comma (expecting UID,Name)
                            parts = data_part.split(',')
                            if len(parts) == 2:
                                uid = parts[0].strip() # Get UID (optional to use)
                                name = parts[1].strip() # Get Name

                                # Check if this name is already logged in this session
                                if not check_if_name_exists(conn, name):
                                    insert_log(conn, current_time, name)
                                else:
                                     print(f"[{current_time}] Duplicate check-in ignored for: {name}")
                            else:
                                print(f"[{current_time}] Warning: Malformed LOG message received: {line}")

                        except Exception as parse_error:
                             print(f"[{current_time}] Error parsing LOG message '{line}': {parse_error}")
                    
                    elif line.startswith(LOGOUT_PREFIX):
                        try:
                            # Extract data part, remove prefix
                            data_part = line[len(LOGOUT_PREFIX):]
                            # Split by comma (expecting UID,Name)
                            parts = data_part.split(',')
                            if len(parts) == 2:
                                uid = parts[0].strip() # Get UID (optional to use)
                                name = parts[1].strip() # Get Name

                                # Delete the user from the logs
                                delete_log(conn, name)
                            else:
                                print(f"[{current_time}] Warning: Malformed LOGOUT message received: {line}")

                        except Exception as parse_error:
                             print(f"[{current_time}] Error parsing LOGOUT message '{line}': {parse_error}")

                    # else: Ignore lines that don't match SESSION_SIGNAL, LOG_PREFIX or LOGOUT_PREFIX
                    # Optional: print ignored lines for debugging
                    # else:
                    #    print(f"[{current_time}] Ignoring: {line}")


            except serial.SerialException as serial_error:
                 print(f"Serial Error: {serial_error}. Attempting to reconnect...")
                 if ser and ser.is_open: ser.close()
                 ser = None # Signal reconnection attempt
                 time.sleep(5) # Wait before retrying connection
            except UnicodeDecodeError as decode_error:
                 print(f"Unicode Decode Error: {decode_error}. Line ignored.")
            except sqlite3.Error as db_error:
                 print(f"Database Error: {db_error}")
                 # Decide how to handle DB errors (e.g., retry, exit)
                 time.sleep(1) # Simple wait
            except Exception as e:
                 print(f"An unexpected error occurred: {e}")
                 time.sleep(1) # Simple wait


    except KeyboardInterrupt:
        print("\nCtrl+C detected. Exiting...")
    finally:
        if ser and ser.is_open:
            ser.close()
            print("Serial port closed.")
        if conn:
            conn.close()
            print("Database connection closed.")

if __name__ == "__main__":
    main()
