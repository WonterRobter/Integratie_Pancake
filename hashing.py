from werkzeug.security import generate_password_hash

password = ""
hashed = generate_password_hash(password)
print(hashed)
