#!/usr/bin/env python3
"""
arduino_logger_db.py  –  v1.1  (2025-05-08)

Listens on a serial port, stores check-ins in SQLite, and removes them again
when a corresponding LOGOUT line arrives, so logged-out students disappear
from the web table.

Author: Jamil Reußwig
"""

import serial
import time
import sqlite3
import sys

# ─── Configuration ──────────────────────────────────────────────────────────
SERIAL_PORT   = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
BAUD_RATE     = 9600
DATABASE      = "arduino_data.db"

LOGIN_PREFIX  = "LOGIN:"      
LEGACY_PREFIX = "LOG:"        
LOGOUT_PREFIX = "LOGOUT:"     
SESSION_SIGNAL = "NEW_SESSION"

# ─── Database helpers ───────────────────────────────────────────────────────
def setup_database():
    conn = sqlite3.connect(DATABASE)
    conn.execute("""
        CREATE TABLE IF NOT EXISTS logs (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT    NOT NULL,
            message   TEXT    NOT NULL
        )
    """)
    conn.commit()
    print(f"[INFO] Database '{DATABASE}' ready.")
    return conn

def clear_logs(conn):
    conn.execute("DELETE FROM logs")
    conn.commit()
    print("[INFO] Log table cleared for new session.")

def name_exists(conn, name):
    return conn.execute("SELECT 1 FROM logs WHERE message = ? LIMIT 1", (name,)).fetchone() is not None

def insert_log(conn, ts, name):
    conn.execute("INSERT INTO logs (timestamp, message) VALUES (?, ?)", (ts, name))
    conn.commit()
    print(f"[{ts}]  LOGIN  → {name}")

def delete_log(conn, ts, name):
    deleted = conn.execute("DELETE FROM logs WHERE message = ?", (name,)).rowcount
    conn.commit()
    if deleted:
        print(f"[{ts}] LOGOUT → {name}  (removed)")
    else:
        print(f"[{ts}] LOGOUT → {name}  (no active entry)")

# ─── Main loop ──────────────────────────────────────────────────────────────
def main():
    conn = setup_database()

    ser = None
    print("[INFO] Connecting to Arduino …")
    while ser is None:
        try:
            ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
            print(f"[INFO] Connected on {SERIAL_PORT}")
            time.sleep(2)          # let the Arduino finish booting
        except serial.SerialException as e:
            print(f"[WARN] {e}  – retrying in 5 s")
            time.sleep(5)
        except KeyboardInterrupt:
            print("\n[ABORT] Interrupted during connection attempt.")
            conn.close()
            return

    print("[INFO] Listening … Ctrl-C to stop.\n")
    try:
        while True:
            try:
                line_bytes = ser.readline()
                if not line_bytes:
                    continue

                line = line_bytes.decode("utf-8", errors="replace").strip()
                if not line:
                    continue

                now = time.strftime("%Y-%m-%d %H:%M:%S")

                # ── Session reset ─────────────────────────────────────────
                if line == SESSION_SIGNAL:
                    clear_logs(conn)
                    continue

                # ── LOGIN (new or legacy) ────────────────────────────────
                if line.startswith(LOGIN_PREFIX) or line.startswith(LEGACY_PREFIX):
                    data = line.split(":", 1)[1]          # remove prefix
                    parts = [p.strip() for p in data.split(",")]
                    if len(parts) == 2:
                        uid, name = parts
                        if not name_exists(conn, name):
                            insert_log(conn, now, name)
                        else:
                            print(f"[{now}] Duplicate login ignored: {name}")
                    else:
                        print(f"[{now}] Malformed LOGIN line: {line}")
                    continue

                # ── LOGOUT ───────────────────────────────────────────────
                if line.startswith(LOGOUT_PREFIX):
                    data = line[len(LOGOUT_PREFIX):]
                    parts = [p.strip() for p in data.split(",")]
                    if len(parts) == 2:
                        uid, name = parts
                        delete_log(conn, now, name)
                    else:
                        print(f"[{now}] Malformed LOGOUT line: {line}")
                    continue

            except serial.SerialException as e:
                print(f"[WARN] Serial error: {e} – reconnecting in 5 s")
                if ser and ser.is_open:
                    ser.close()
                ser = None
                time.sleep(5)
                continue

    except KeyboardInterrupt:
        print("\n[INFO] Stopping …")
    finally:
        if ser and ser.is_open:
            ser.close()
        conn.close()

if __name__ == "__main__":
    main()
