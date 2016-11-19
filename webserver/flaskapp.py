from flask import Flask, render_template, abort, g
from datetime import datetime
import sqlite3

app = Flask(__name__)

DATABASE = 'logs.db'


def get_db_connection():
    database = getattr(g, '_database', None)
    if database is None:
        database = g._database = sqlite3.connect(DATABASE, detect_types=sqlite3.PARSE_DECLTYPES)
    return database


@app.teardown_appcontext
def close_database_connection(exception):
    database = getattr(g, '_database', None)
    if database is not None:
        database.close()


class Event: CHECK_IN, CHECK_OUT, ALARM = range(3)

@app.context_processor
def utility_processor():
    def get_event_info(event):
        if event == Event.CHECK_IN:
            return dict(description="checked in", color="success")
        elif event == Event.CHECK_OUT:
            return dict(description="checked out", color="warning")
        elif event == Event.ALARM:
            return dict(description="alarm triggered", color="danger")
        else:
            return dict(description="", color="info")
    return dict(get_event_info=get_event_info)


@app.route('/')
def main_page():
    cur = get_db_connection().cursor()
    cur.execute('select rfid, event, time from log')
    db_rows = cur.fetchall()
    cur.close()
    return render_template('table.html', log_rows=db_rows)


@app.route('/add/<rfid>/<action>')
def add_entry(rfid, action):
    event = None
    if action == 'i':
        event = Event.CHECK_IN
    elif action == 'o':
        event = Event.CHECK_OUT
    elif action == 'a':
        event = Event.ALARM
    else:
        abort(400, 'invalid action')
    connection = get_db_connection()
    cur = connection.cursor()
    cur.execute('insert into log values(?, ?, ?)', (rfid, event, datetime.now()))
    cur.close()
    connection.commit()
    return 'OK\r\n'

if __name__ == '__main__':
    app.run()
