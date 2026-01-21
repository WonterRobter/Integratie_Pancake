# ------------------ BRONNEN ------------------ #
# Zie https://github.com/WonterRobter/Integratie_Pancake/ voor bronnenlijst
# ------------------ Libraries ------------------ #
from flask import Flask, render_template, request, redirect, url_for, session, jsonify
from werkzeug.security import generate_password_hash, check_password_hash
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import mysql.connector
import os
from datetime import datetime
from dotenv import load_dotenv

load_dotenv()

app = Flask(__name__)
app.secret_key = os.getenv('FLASK_SECRET', 'dev_secret')

# ------------------ DATABASE ------------------ #

def get_db():
    return mysql.connector.connect(
        host=os.getenv('DB_HOST'),
        port=int(os.getenv('DB_PORT')),
        user=os.getenv('DB_USER'),
        password=os.getenv('DB_PASS'),
        database=os.getenv('DB_NAME')
    )

# ------------------ AUTH FUNCTIES ------------------ #

def login_required():
    return 'user_id' in session

def is_admin():
    return login_required() and session.get('role') == 'admin'

def parent_or_admin_required():
    role = session.get('role')
    return login_required() and (role == 'admin' or role == 'parent')

# ------------------ LOGIN / LOGOUT ------------------ #

@app.route("/", methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        username = request.form['username']
        password = request.form['password']

        db = get_db()
        cursor = db.cursor(dictionary=True)
        cursor.execute("SELECT * FROM users WHERE username=%s", (username,))
        user = cursor.fetchone()
        cursor.close()
        db.close()

        if user and check_password_hash(user['password_hash'], password):
            # Eerste login met standaard wachtwoord
            if password == "Wachtwoord123":
                session['user_id'] = user['user_id']
                session['role'] = user['role']
                session['family_id'] = user['Family_id']
                session['force_password_change'] = True
                return redirect(url_for('change_password'))

            # Normale login
            session['user_id'] = user['user_id']
            session['role'] = user['role']
            session['family_id'] = user['Family_id']
            return redirect(url_for('index'))
        else:
            return render_template('login.html', error="Ongeldige login")

    if 'user_id' in session:
        return redirect(url_for('index'))

    return render_template('login.html', error=None)

@app.route("/logout")
def logout():
    session.clear()
    return redirect(url_for('login'))

# ------------------ PASSWORD CHANGE ------------------ #

@app.route("/change_password", methods=['GET', 'POST'])
def change_password():
    if not login_required():
        return redirect(url_for('login'))

    force_change = session.get('force_password_change', False)
    if not force_change:
        return redirect(url_for('index'))

    if request.method == 'POST':
        old_password = "Wachtwoord123"
        new_password = request.form['new_password']
        confirm_password = request.form['confirm_password']

        if new_password == old_password:
            return render_template(
                'change_password.html',
                error="Nieuw wachtwoord moet anders zijn dan het standaard wachtwoord"
            )

        if new_password != confirm_password:
            return render_template('change_password.html', error="Wachtwoorden komen niet overeen")

        if len(new_password) < 6:
            return render_template('change_password.html', error="Wachtwoord moet minstens 6 tekens zijn")

        hashed = generate_password_hash(new_password)
        db = get_db()
        cursor = db.cursor()
        cursor.execute("UPDATE users SET password_hash=%s WHERE user_id=%s",
                       (hashed, session['user_id']))
        db.commit()
        cursor.close()
        db.close()

        session.pop('force_password_change', None)
        return redirect(url_for('index'))

    return render_template('change_password.html', force_change=True)

@app.route("/change_own_password", methods=['GET', 'POST'])
def change_own_password():
    if not login_required():
        return redirect(url_for('login'))

    if request.method == 'POST':
        current_password = request.form['current_password']
        new_password = request.form['new_password']
        confirm_password = request.form['confirm_password']

        db = get_db()
        cursor = db.cursor(dictionary=True)
        cursor.execute("SELECT password_hash FROM users WHERE user_id=%s", (session['user_id'],))
        user = cursor.fetchone()

        if not user or not check_password_hash(user['password_hash'], current_password):
            db.close()
            return render_template('change_own_password.html', error="Huidig wachtwoord onjuist")

        if new_password == current_password:
            db.close()
            return render_template('change_own_password.html', error="Nieuw wachtwoord moet anders zijn dan huidig wachtwoord")

        if new_password != confirm_password:
            db.close()
            return render_template('change_own_password.html', error="Wachtwoorden komen niet overeen")

        if len(new_password) < 6:
            db.close()
            return render_template('change_own_password.html', error="Wachtwoord moet minstens 6 tekens zijn")

        hashed = generate_password_hash(new_password)
        cursor = db.cursor()
        cursor.execute("UPDATE users SET password_hash=%s WHERE user_id=%s",
                       (hashed, session['user_id']))
        db.commit()
        cursor.close()
        db.close()

        return redirect(url_for('index'))

    return render_template('change_own_password.html')

# ------------------ DASHBOARD ------------------ #

@app.route("/index")
@app.route("/dashboard")
def index():
    if not login_required():
        return redirect(url_for('login'))

    db = get_db()
    cursor = db.cursor(dictionary=True)

    cursor.execute("""
        SELECT sl.*, p.name AS program_name
        FROM sensor_logs sl
        LEFT JOIN programs p ON sl.program_id = p.program_id
        WHERE sl.temperature IS NOT NULL
        ORDER BY sl.timestamp DESC
        LIMIT 3
    """)
    recent_sensors = cursor.fetchall()

    cursor.close()
    db.close()

    return render_template(
        "index.html",
        role=session.get("role"),
        recent_sensors=recent_sensors
    )

# ------------------ USERS ------------------ #

@app.route("/users")
def users():
    if not parent_or_admin_required():
        return redirect(url_for('index'))

    db = get_db()
    cursor = db.cursor(dictionary=True)

    if is_admin():
        cursor.execute("SELECT * FROM users ORDER BY user_id")
    else:
        cursor.execute("""
            SELECT * FROM users 
            WHERE Family_id = %s OR user_id = %s
            ORDER BY user_id
        """, (session['family_id'], session['user_id']))
    users = cursor.fetchall()

    cursor.close()
    db.close()

    return render_template('users.html', users=users, role=session['role'])

@app.route("/add_user", methods=['POST'])
def add_user():
    if not parent_or_admin_required():
        return redirect(url_for('users'))

    username = request.form['username']
    role = request.form['role']

    default_password = "Wachtwoord123"
    hashed = generate_password_hash(default_password)

    db = get_db()
    cursor = db.cursor()

    if is_admin():
        family_id = request.form.get('family_id', '0')
    else:
        family_id = str(session['family_id'])

    cursor.execute(
        "INSERT INTO users (username, password_hash, role, Family_id) VALUES (%s, %s, %s, %s)",
        (username, hashed, role, family_id)
    )
    db.commit()
    cursor.close()
    db.close()

    return redirect(url_for('users'))

@app.route("/delete_user/<int:user_id>")
def delete_user(user_id):
    if not parent_or_admin_required():
        return redirect(url_for('users'))

    if user_id == 1:
        return redirect(url_for('users', error="Admin gebruiker kan niet verwijderd worden"))

    db = get_db()
    cursor = db.cursor()

    if is_admin():
        cursor.execute("DELETE FROM users WHERE user_id=%s", (user_id,))
    else:
        cursor.execute(
            "DELETE FROM users WHERE user_id=%s AND Family_id=%s",
            (user_id, session['family_id'])
        )

    db.commit()
    cursor.close()
    db.close()

    return redirect(url_for('users'))

# ------------------ PROGRAMS ------------------ #

@app.route("/programs")
def programs():
    if not login_required():
        return redirect(url_for('login'))

    db = get_db()
    cursor = db.cursor(dictionary=True)

    if is_admin():
        cursor.execute("SELECT * FROM programs ORDER BY name")
    else:
        cursor.execute("""
            SELECT p.* FROM programs p
            JOIN users u ON p.user_id = u.user_id
            WHERE u.Family_id = %s OR u.user_id = %s
            ORDER BY p.name
        """, (session['family_id'], session['user_id']))
    programs = cursor.fetchall()

    cursor.close()
    db.close()

    return render_template("programs.html", programs=programs, role=session['role'])
    

@app.route("/logs")
def logs():
    if not login_required():
        return redirect(url_for('login'))

    db = get_db()
    cursor = db.cursor(dictionary=True)

    cursor.execute("""
        SELECT sl.log_id,
               sl.timestamp,
               sl.temperature,
               sl.action,
               sl.program_id,
               s.session_id,
               p.name AS program_name
        FROM sensor_logs sl
        LEFT JOIN sessions s ON sl.session_id = s.session_id
        LEFT JOIN programs p ON sl.program_id = p.program_id
        WHERE sl.temperature IS NOT NULL
        ORDER BY sl.timestamp DESC
        LIMIT 1000
    """)
    logs = cursor.fetchall()

    cursor.close()
    db.close()

    return render_template("logs.html",
                           role=session.get("role"),
                           logs=logs)


@app.route("/add_program", methods=['POST'])
def add_program():
    if not parent_or_admin_required():
        return redirect(url_for('programs'))

    name = request.form['name']
    description = request.form.get('description', '')
    target_temp = request.form['target_temp']
    flip_time = request.form['flip_time']
    total_time = request.form['total_time']

    db = get_db()
    cursor = db.cursor()
    cursor.execute("""
        INSERT INTO programs (user_id, name, description, target_temp, total_time, flip_time)
        VALUES (%s, %s, %s, %s, %s, %s)
    """, (session['user_id'], name, description, target_temp, total_time, flip_time))
    db.commit()
    cursor.close()
    db.close()

    return redirect(url_for('programs'))

@app.route("/delete_program/<int:program_id>", methods=['POST'])
def delete_program(program_id):
    if not parent_or_admin_required():
        return redirect(url_for('programs'))

    db = get_db()
    cursor = db.cursor()
    if is_admin():
        cursor.execute("DELETE FROM programs WHERE program_id=%s", (program_id,))
    else:
        cursor.execute("""
            DELETE p FROM programs p
            JOIN users u ON p.user_id = u.user_id
            WHERE p.program_id=%s AND (u.Family_id=%s OR u.user_id=%s)
        """, (program_id, session['family_id'], session['user_id']))
    db.commit()
    cursor.close()
    db.close()

    return redirect(url_for('programs'))

# ------------------ ARDUINO API ------------------ #

@app.route("/api/start_session", methods=['POST'])
def start_session():
    data = request.get_json(force=True, silent=True) or {}
    program_id = data.get('program_id')

    print("start_session data:", data)

    db = get_db()
    cursor = db.cursor()
    user_id = 1  # voorlopig vaste user (admin of demo-user)

    try:
        cursor.execute("""
            INSERT INTO sessions (user_id, program_id, start_time)
            VALUES (%s, %s, NOW())
        """, (user_id, program_id))
        session_id = cursor.lastrowid
        db.commit()
    except mysql.connector.Error as e:
        print("DB error in start_session:", e)
        db.rollback()
        cursor.close()
        db.close()
        return jsonify({"error": "db_error", "message": str(e)}), 500

    cursor.close()
    db.close()

    return jsonify({"session_id": session_id}), 200

@app.route("/api/programs")
def api_programs():
    db = get_db()
    cursor = db.cursor(dictionary=True)
    cursor.execute("""
        SELECT program_id, name, target_temp, flip_time, total_time
        FROM programs
        ORDER BY program_id
    """)
    rows = cursor.fetchall()
    cursor.close()
    db.close()
    return jsonify(rows)



@app.route("/api/sensor_data", methods=['POST'])
def sensor_data():
    data = request.get_json(force=True, silent=True) or {}
    print("Arduino data:", data)

    temperature = data.get('temperature')
    if temperature is None:
        return {"status": "ignored", "reason": "no_temperature"}, 200

    db = get_db()
    cursor = db.cursor()

    cursor.execute("""
        INSERT INTO sensor_logs (session_id, timestamp, temperature, action, program_id)
        VALUES (%s, NOW(), %s, %s, %s)
    """, (
        data.get('session_id'),
        temperature,
        data.get('status'),     # 'preheat' | 'cook' | 'wait' | 'stop'
        data.get('program_id')
    ))

    db.commit()
    cursor.close()
    db.close()

    return {"status": "ok", "received": data}, 200

@app.route("/api/control")
def get_control():
    return {
        "relay": False,
        "led_r": 0,
        "led_g": 255,
        "led_b": 0
    }

# ------------------ csv export ------------------ #

@app.route("/logs/csv")
def logs_csv():
    if not parent_or_admin_required():
        return redirect(url_for('index'))

    db = get_db()
    cursor = db.cursor(dictionary=True)
    cursor.execute("""
        SELECT sl.log_id,
               sl.timestamp,
               sl.temperature,
               sl.action,
               sl.program_id,
               s.session_id,
               p.name AS program_name
        FROM sensor_logs sl
        LEFT JOIN sessions s ON sl.session_id = s.session_id
        LEFT JOIN programs p ON sl.program_id = p.program_id
        ORDER BY sl.timestamp DESC
    """)
    rows = cursor.fetchall()
    cursor.close()
    db.close()

    # CSV-string opbouwen
    import csv
    import io
    output = io.StringIO()
    writer = csv.writer(output, delimiter=';')
    writer.writerow(["log_id", "timestamp", "temperature", "action",
                     "program_id", "session_id", "program_name"])
    for r in rows:
        writer.writerow([
            r["log_id"], r["timestamp"], r["temperature"], r["action"],
            r["program_id"], r["session_id"], r["program_name"]
        ])
    csv_data = output.getvalue()
    output.close()

    from flask import Response
    return Response(
        csv_data,
        mimetype="text/csv",
        headers={"Content-Disposition": "attachment;filename=sensor_logs.csv"}
    )


# ------------------ GRAPH ------------------ #

@app.route("/graph")
def graph():
    if not login_required():
        return redirect(url_for('login'))

    db = get_db()
    cursor = db.cursor(dictionary=True)

    cursor.execute("""
        SELECT  t.timestamp,
        t.temperature
        FROM (
        SELECT timestamp, temperature
        FROM sensor_logs
        WHERE program_id IS NOT NULL
        AND temperature IS NOT NULL
        ORDER BY timestamp DESC
        LIMIT 50
        ) AS t
        ORDER BY t.timestamp ASC;
    """)
    rows = cursor.fetchall()
    cursor.close()
    db.close()

    labels = [r['timestamp'].strftime("%H:%M:%S") for r in rows]
    temps  = [float(r['temperature']) for r in rows]

    return render_template('graph.html', role=session.get('role'),
                           labels=labels, temps=temps)

# ------------------ MAIN ------------------ #

if __name__ == "__main__":
    app.run(host='0.0.0.0', port=5000, debug=True)
