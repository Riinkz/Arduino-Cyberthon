# RFID Anwesenheitssystem - Cyberthon 2025

## Projektübersicht

Dieses Projekt wurde im Rahmen des **Cyberthon 2025** Wettbewerbs entwickelt. Ziel ist die Erstellung eines Prototyps für ein automatisiertes Anwesenheitssystem mittels RFID-Technologie. Es soll den manuellen Prozess der Anwesenheitskontrolle in Schulen oder Unternehmen ersetzen, um Zeit zu sparen und die Effizienz zu steigern.

Das System ermöglicht es autorisierten Personen (Schülern, Mitarbeitern), sich durch Scannen einer RFID-Karte einzuchecken. Die Anwesenheitsdaten werden für eine definierte Sitzungsdauer erfasst und über eine Weboberfläche angezeigt.

**Autoren:** Dario Neumann, Jamil Reußwig, Moritz Schulze

---

## Systemarchitektur

Das System besteht aus zwei Hauptkomponenten:

1.  **Arduino Mega 2560:**
    * Dient als Erfassungs- und Steuerungseinheit.
    * Liest RFID-Karten über ein MFRC522 Modul.
    * Zeigt Statusinformationen auf einem 16x2 LCD und die verbleibende Sitzungszeit auf einem 4-stelligen 7-Segment-Display an.
    * Signalisiert Erfolg (grüne LED) oder Fehler (rote LED) beim Scan.
    * Startet eine neue Anwesenheitssitzung und den Countdown-Timer über einen Tasterdruck.
    * Sendet Signale für den Sitzungsstart (`NEW_SESSION`) und erfolgreiche, autorisierte Check-ins (`LOG:<UID>,<Name>`) über die serielle Schnittstelle (USB).

2.  **Raspberry Pi:**
    * Dient als Server für die Datenverarbeitung und Webanzeige.
    * **Logger Script (`arduino_logger_db.py`):**
        * Empfängt Daten vom Arduino über die serielle Schnittstelle.
        * Bei Empfang von `"NEW_SESSION"`: Löscht alle Einträge aus der SQLite-Datenbank (`arduino_data.db`), um die vorherige Sitzung zu beenden.
        * Bei Empfang von `"LOG:<UID>,<Name>"`: Prüft, ob dieser Name bereits in der aktuellen Sitzung (d.h. in der nun leeren/teilweise gefüllten Tabelle) vorhanden ist. Wenn nicht, wird der aktuelle Zeitstempel und der Name in die Datenbank gespeichert.
    * **Web App Script (`arduino_webapp.py`):**
        * Ein einfacher Flask-Webserver.
        * Liest die aktuellen Einträge aus der SQLite-Datenbank.
        * Stellt die Daten über eine HTML-Seite (`templates/index.html`) im Browser dar. Die Seite aktualisiert sich automatisch alle 10 Sekunden.

---

## Hardware-Komponenten

* Arduino Mega 2560 R3
* Raspberry Pi (getestet mit RPi 5, andere sollten funktionieren)
* MFRC522 RFID Reader Modul + RFID Karten/Tags (13.56 MHz Mifare)
* 16x2 LCD Display mit I2C Interface
* 4-Digit 7-Segment Display (Common Cathode)
* 1x Grüne LED
* 1x Rote LED
* 1x Taster (Pushbutton)
* Widerstände für LEDs (z.B. 220 Ohm oder 330 Ohm)
* Breadboard und Verbindungskabel
* USB-Kabel (Arduino zu Raspberry Pi)

---

## Software & Bibliotheken

* **Arduino:**
    * `RFID_Attendance_System.ino` (Der Hauptsketch)
    * Benötigte Arduino Bibliotheken:
        * `LiquidCrystal_I2C` (via Library Manager)
        * `SevSeg` (via Library Manager)
        * `MFRC522` (von miguelbalboa, via Library Manager)
        * `SPI` (Standard, integriert)
* **Raspberry Pi:**
    * Python 3
    * `arduino_logger_db.py` (Serial Listener & DB Logger)
    * `arduino_webapp.py` (Flask Web Server)
    * `templates/index.html` (HTML Frontend)
    * Benötigte Python Bibliotheken:
        * `pyserial` (`pip install pyserial`)
        * `Flask` (`pip install Flask`)
        * `sqlite3` (Standard, integriert)

---

## Setup & Installation

1.  **Hardware:**
    * Verbinde  die Komponenten (RFID, LCD, 7-Segment, LEDs, Button) mit dem Arduino Mega gemäß den Pin-Definitionen im `.ino` Sketch. Stelle  sicher, dass die LEDs Vorwiderstände haben.
    * Verbinde  den Arduino Mega über ein USB-Kabel mit dem Raspberry Pi.

2.  **Arduino:**
    * Öffne  die Arduino IDE.
    * Installiere  die benötigten Bibliotheken (`LiquidCrystal_I2C`, `SevSeg`, `MFRC522`) über den Library Manager (Sketch -> Include Library -> Manage Libraries...).
    * Öffne  den `RFID_Attendance_System.ino` Sketch.
    * **WICHTIG:** Passe  die Arrays `authorizedUIDs` und `names` im Sketch an Ihre RFID-Karten und die zugehörigen Namen an. Der UID-String muss dem Format entsprechen, das beim Scannen einer Karte auf dem Serial Monitor ausgegeben wird (dezimale Bytewerte mit Leerzeichen getrennt).
    * Wähle  das richtige Board (Arduino Mega or Mega 2560) und den Port in der Arduino IDE aus.
    * Lade  den Sketch auf den Arduino hoch.

3.  **Raspberry Pi:**
    * Stelle  sicher, dass Python 3 und pip installiert sind.
    * Installiere  die benötigten Python-Bibliotheken:
        ```bash
        pip install pyserial flask
        ```
    * Erstelle  ein Verzeichnis für das Projekt, z.B. `~/Documents/AWHS`.
    * Platziere  `arduino_logger_db.py` und `arduino_webapp.py` in diesem Verzeichnis.
    * Erstelle  ein Unterverzeichnis `templates` im Projektverzeichnis.
    * Platziere  `index.html` in das `templates` Verzeichnis.
    * Identifiziere  den seriellen Port, an dem der Arduino angeschlossen ist. Normalerweise `/dev/ttyACM0` oder `/dev/ttyACM1`. Mann kann diese mit `ls /dev/ttyACM*` überprüfen, nachdem der Arduino verbunden ist.
    * **WICHTIG:** Bearbeite  `arduino_logger_db.py` und passe ggf. die Variable `SERIAL_PORT` an den gefundenen Port an (oder übergebe  den Port als Kommandozeilenargument beim Start).

---

## System starten & verwenden

1.  **Logger starten:** Öffne ein Terminal auf dem Raspberry Pi und navigiere zum Projektverzeichnis. Starte das Logger-Skript:
    ```bash
    # Beispiel: Standardport /dev/ttyACM0
    python3 arduino_logger_db.py

    # Beispiel: Port explizit angeben
    python3 arduino_logger_db.py /dev/ttyACM1
    ```
    Das Skript initialisiert die Datenbank und wartet auf Daten vom Arduino.

2.  **Web App starten:** Öffne ein *zweites* Terminal auf dem Raspberry Pi (oder verwende  `tmux` / `screen`), navigiere  zum Projektverzeichnis und starte  den Flask-Webserver:
    ```bash
    python3 arduino_webapp.py
    ```
    Der Server startet und ist im lokalen Netzwerk unter Port 5000 erreichbar.

3.  **Web Interface aufrufen:** Öffne einen Webbrowser auf einem Gerät im selben Netzwerk wie der Raspberry Pi und navigiere  zu:
    `http://<IP_ADRESSE_DES_RASPBERRY_PI>:5000`
    (Ersetze `<IP_ADRESSE_DES_RASPBERRY_PI>` mit der tatsächlichen IP-Adresse Ihres Raspberry Pi. Kann man mit `hostname -I` im Terminal finden).

4.  **Sitzung starten:** Drücke  den Taster am Arduino. Das 7-Segment-Display beginnt mit dem Countdown, die LCD-Anzeige wechselt zu "Countdown Laeuft", und die Log-Tabelle im Web Interface wird für die neue Sitzung geleert.

5.  **Anwesenheit erfassen:** Autorisierte Personen scannen ihre RFID-Karte am Reader.
    * Bei Erfolg leuchtet die grüne LED kurz auf, der Name wird auf dem LCD angezeigt, und der Name wird (einmalig pro Sitzung) im Web Interface mit Zeitstempel geloggt.
    * Bei einer unbekannten Karte leuchtet die rote LED kurz auf, und das LCD zeigt "Karte ungueltig" an. Es erfolgt kein Log-Eintrag.

6.  **Beobachten:** Das Web Interface aktualisiert sich alle 10 Sekunden automatisch und zeigt die neuesten Check-ins der aktuellen Sitzung an.

7.  **Sitzungsende:** Wenn der Countdown auf dem 7-Segment-Display Null erreicht, kehrt das System in den Ausgangszustand zurück (wartet auf Tasterdruck). Die Logs der beendeten Sitzung bleiben in der Datenbank, bis der Taster erneut gedrückt wird, um eine *neue* Sitzung zu starten.

---

## Funktionsweise der Logik

* **Session Management:** Der Arduino sendet das Signal `"NEW_SESSION"` nur beim initialen Tasterdruck, der eine neue Countdown-Periode startet. Der Python-Logger löscht daraufhin die `logs`-Tabelle in der SQLite-Datenbank.
* **Datenlogging:** Der Arduino sendet nur bei erfolgreicher Erkennung einer *autorisierten* Karte eine Nachricht im Format `"LOG:<UID>,<Name>"`.
* **Duplikatvermeidung:** Der Python-Logger prüft vor dem Einfügen eines neuen Eintrags, ob der `Name` bereits in der `logs`-Tabelle existiert (seit dem letzten `"NEW_SESSION"`-Signal). Nur wenn der Name noch nicht vorhanden ist, wird der neue Eintrag mit Zeitstempel und Name hinzugefügt.

