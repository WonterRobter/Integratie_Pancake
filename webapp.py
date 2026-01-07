from flask import Flask, render_template, request, redirect, session
import matplotlib.pyplot as plt
import random
import os

app = Flask(__name__)
app.secret_key = "pancake_secret"

# ---------------- USERS (tijdelijk, RAM)
USERS = {
    "admin": {"password": "admin123", "role": "admin"},
    "ouder": {"password": "ouder123", "role": "user"}
}

# ---------------- PROGRAMS (tijdelijk, RAM)
PROGRAMS = []

# ---------------- GRAFIEK
def make_chart():
    temps = [random.randint(40, 180) for _ in range(10)]
    os.makedirs("static", exist_ok=True)

    plt.figure()
    plt.plot(temps, marker="o")
    plt.title("Temperatuur simulatie")
    plt.xlabel("Tijd")
    plt.ylabel("°C")
    plt.grid(True)
    plt.savefig("static/chart.png")
    plt.close()

# ---------------- ROUTES
@app.route("/", methods=["GET", "POST"])
def login():
    if request.method == "POST":
        u = request.form["username"]
        p = request.form["password"]
        if u in USERS and USERS[u]["password"] == p:
            session["user"] = u
            session["role"] = USERS[u]["role"]
            return redirect("/dashboard")
    return render_template("login.html")


@app.route("/dashboard")
def dashboard():
    if "user" not in session:
        return redirect("/")
    make_chart()
    return render_template("dashboard.html")


@app.route("/programs", methods=["GET", "POST"])
def programs():
    if "user" not in session:
        return redirect("/")

    if request.method == "POST" and session["role"] == "admin":
        PROGRAMS.append({
            "name": request.form["name"],
            "temp": request.form["temp"],
            "flip": request.form["flip"],
            "time": request.form["time"]
        })

    return render_template("programs.html", programs=PROGRAMS, role=session["role"])


@app.route("/delete_program/<int:index>")
def delete_program(index):
    if session.get("role") == "admin":
        PROGRAMS.pop(index)
    return redirect("/programs")


@app.route("/users", methods=["GET", "POST"])
def users():
    if session.get("role") != "admin":
        return redirect("/dashboard")

    if request.method == "POST":
        USERS[request.form["username"]] = {
            "password": request.form["password"],
            "role": request.form["role"]
        }

    return render_template("users.html", users=USERS)


@app.route("/delete_user/<username>")
def delete_user(username):
    if session.get("role") == "admin" and username != "admin":
        USERS.pop(username)
    return redirect("/users")


@app.route("/logout")
def logout():
    session.clear()
    return redirect("/")


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)
