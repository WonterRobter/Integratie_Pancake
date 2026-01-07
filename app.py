from flask import Flask, render_template, request, redirect, url_for, session, Response
import mysql.connector
from werkzeug.security import generate_password_hash, check_password_hash
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import io
from dotenv import load_dotenv
import os

# ----------------- ENV -----------------
load_dotenv()
DB_HOST = os.getenv('DB_HOST')
DB_USER = os.getenv('DB_USER')
DB_PASS = os.getenv('DB_PASS')
DB_NAME = os.getenv('DB_NAME')
FLASK_SECRET = os.getenv('FLASK_SECRET')

app = Flask(__name__)
app.secret_key = FLASK_SECRET


# ----------------- HELPERS -----------------
def get_db():
    return mysql.connector.connect(
        host=DB_HOST,
        user=DB_USER,
        password=DB_PASS,
        database=DB_NAME
    )


def login_required():
    return 'user_id' in session


def is_admin():
    return session.get('role') == 'admin'


def is_parent():
    return session.get('role') == 'parent'


def is_child():
    return session.get('role') == 'child'


def parent_or_admin_required():
    return login_required() and (is_parent() or is_admin())


# ----------------- LOGIN -----------------
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
            session['user_id'] = user['user_id']
            session['role'] = user['role']
            # In DB: Family_id (met hoofdletter F)
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


# ----------------- STARTPAGINA -----------------
@app.route("/index")
def index():
    if not login_required():
        return redirect(url_for('login'))
    return render_template('index.html', role=session.get('role'))


# ----------------- PROGRAMMA'S -----------------
@app.route("/dashboard")
def dashboard():
    if not login_required():
        return redirect(url_for('login'))

    db = get_db()
    cursor = db.cursor(dictionary=True)
    cursor.execute(
        """
        SELECT p.program_id, p.user_id, p.name, p.description,
               p.target_temp, p.total_time, p.flip_time,
               u.username, u.Family_id
        FROM programs p
        JOIN users u ON p.user_id = u.user_id
        WHERE u.Family_id = %s
        ORDER BY p.program_id ASC
        """,
        (session['family_id'],)
    )
    programs = cursor.fetchall()
    cursor.close()
    db.close()

    return render_template('dashboard.html',
                           programs=programs,
                           role=session.get('role'))


@app.route("/add_program", methods=['POST'])
def add_program():
    if not parent_or_admin_required():
        return redirect(url_for('dashboard'))

    name = request.form['name']
    description = request.form.get('description', '')
    target_temp = request.form['target_temp']
    total_time = request.form.get('total_time', 0)
    flip_time = request.form.get('flip_time', 0)

    db = get_db()
    cursor = db.cursor()
    cursor.execute(
        """
        INSERT INTO programs (user_id, name, description, target_temp, total_time, flip_time)
        VALUES (%s, %s, %s, %s, %s, %s)
        """,
        (session['user_id'], name, description, target_temp, total_time, flip_time)
    )
    db.commit()
    cursor.close()
    db.close()

    return redirect(url_for('dashboard'))


@app.route("/edit_program/<int:program_id>", methods=['GET', 'POST'])
def edit_program(program_id):
    if not parent_or_admin_required():
        return redirect(url_for('dashboard'))

    db = get_db()
    cursor = db.cursor(dictionary=True)

    if request.method == 'POST':
        name = request.form['name']
        description = request.form.get('description', '')
        target_temp = request.form['target_temp']
        total_time = request.form.get('total_time', 0)
        flip_time = request.form.get('flip_time', 0)

        cursor.execute(
            """
            UPDATE programs
            SET name=%s, description=%s, target_temp=%s, total_time=%s, flip_time=%s
            WHERE program_id=%s
            """,
            (name, description, target_temp, total_time, flip_time, program_id)
        )
        db.commit()
        cursor.close()
        db.close()
        return redirect(url_for('dashboard'))

    cursor.execute(
        "SELECT * FROM programs WHERE program_id=%s",
        (program_id,)
    )
    program = cursor.fetchone()
    cursor.close()
    db.close()

    if not program:
        return redirect(url_for('dashboard'))

    return render_template('edit_program.html', program=program, role=session.get('role'))


@app.route("/delete_program/<int:program_id>")
def delete_program(program_id):
    if not parent_or_admin_required():
        return redirect(url_for('dashboard'))

    db = get_db()
    cursor = db.cursor()

    if is_admin():
        cursor.execute(
            "DELETE FROM programs WHERE program_id=%s",
            (program_id,)
        )
    else:
        cursor.execute(
            """
            DELETE p FROM programs p
            JOIN users u ON p.user_id = u.user_id
            WHERE p.program_id=%s AND u.Family_id=%s
            """,
            (program_id, session['family_id'])
        )

    db.commit()
    cursor.close()
    db.close()

    return redirect(url_for('dashboard'))


# ----------------- USERS -----------------
@app.route("/users")
def users():
    if not login_required():
        return redirect(url_for('login'))

    db = get_db()
    cursor = db.cursor(dictionary=True)

    if is_admin():
        cursor.execute("SELECT user_id, username, role, Family_id FROM users")
    elif is_parent():
        cursor.execute(
            "SELECT user_id, username, role, Family_id FROM users WHERE Family_id=%s",
            (session['family_id'],)
        )
    else:
        cursor.close()
        db.close()
        return redirect(url_for('index'))

    users_list = cursor.fetchall()
    cursor.close()
    db.close()

    return render_template('users.html', users=users_list, role=session.get('role'))


@app.route("/add_user", methods=['POST'])
def add_user():
    if not parent_or_admin_required():
        return redirect(url_for('users'))

    username = request.form['username']
    password = request.form['password']
    role = request.form['role']
    hashed = generate_password_hash(password)

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

    db = get_db()
    cursor = db.cursor()

    if is_admin():
        cursor.execute(
            "DELETE FROM users WHERE user_id=%s",
            (user_id,)
        )
    else:
        cursor.execute(
            "DELETE FROM users WHERE user_id=%s AND Family_id=%s",
            (user_id, session['family_id'])
        )

    db.commit()
    cursor.close()
    db.close()

    return redirect(url_for('users'))


# ----------------- LIVE GRAFIEK -----------------
@app.route("/graph")
def graph_page():
    if not login_required():
        return redirect(url_for('login'))
    return render_template('graph.html', role=session.get('role'))


@app.route("/graph.png")
def graph_png():
    if not login_required():
        return redirect(url_for('login'))

    db = get_db()
    cursor = db.cursor()
    cursor.execute("""
        SELECT timestamp, temperature
        FROM sensor_logs
        ORDER BY timestamp DESC
        LIMIT 20
    """)
    data = cursor.fetchall()
    cursor.close()
    db.close()

    if not data:
        fig, ax = plt.subplots(figsize=(6, 3))
        ax.text(0.5, 0.5, "Nog geen sensor data",
                ha='center', va='center', fontsize=12)
        ax.set_axis_off()
    else:
        data = list(data)
        data.reverse()

        times = [str(row[0]) for row in data]
        temps = [row[1] for row in data]

        fig, ax = plt.subplots(figsize=(6, 3))
        ax.plot(times, temps, marker='o')
        ax.set_xlabel("Tijd")
        ax.set_ylabel("Temperatuur (°C)")
        plt.xticks(rotation=45)
        plt.tight_layout()

    buf = io.BytesIO()
    plt.savefig(buf, format='png')
    plt.close(fig)
    buf.seek(0)
    return Response(buf.getvalue(), mimetype='image/png')


# ================== MAIN ==================
if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)
