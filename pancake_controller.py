import os
from dotenv import load_dotenv
import mysql.connector

load_dotenv()

conn = mysql.connector.connect(
    host=os.getenv("DB_HOST"),
    user=os.getenv("DB_USER"),
    password=os.getenv("DB_PASS"),
    database=os.getenv("DB_NAME")
)

cursor = conn.cursor()

sensor_id = input("Voer het sensor ID in: ")
reading_value = input("Voer de metingwaarde in: ")

sql = "INSERT INTO sensor_readings (sensor_id, reading_value) VALUES (%s, %s)"
cursor.execute(sql, (sensor_id, reading_value))
conn.commit()

print("Nieuwe meting toegevoegd")

cursor.close()
conn.close()
