from flask import Flask, render_template
import sqlite3

app = Flask(__name__)
DATABASE = "arduino_data.db"

def get_db_connection():
    conn = sqlite3.connect(DATABASE)
    conn.row_factory = sqlite3.Row  # This allows accessing columns by name.
    return conn

@app.route('/')
def index():
    conn = get_db_connection()
    logs = conn.execute('SELECT * FROM logs ORDER BY id DESC').fetchall()
    conn.close()
    return render_template('index.html', logs=logs)

if __name__ == '__main__':
    # Run on all network interfaces on port 5000.
    app.run(host='0.0.0.0', port=5000, debug=True)
