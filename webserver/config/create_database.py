import sqlite3
db = sqlite3.connect('logs.db', detect_types=sqlite3.PARSE_DECLTYPES)
cursor = db.cursor()
try:
	cursor.execute('create table log (rfid integer, event integer , time timestamp)')
except:
	print "Error creating the database. perhaps it already exits?"
finally:
	cursor.close()
	db.close()
