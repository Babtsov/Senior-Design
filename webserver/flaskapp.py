from flask import Flask, render_template, abort, g
from datetime import datetime, timedelta
import sqlite3

DATABASE = '/data/logs.db'

app = Flask(__name__)


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


class Event: CHECK_IN, CHECK_OUT, ALARM, REGISTERED, BOOT = range(5)

@app.context_processor
def utility_processor():
    def get_event_info(event):
        if event == Event.CHECK_IN:
            return dict(description="checked in", color="success")
        elif event == Event.CHECK_OUT:
            return dict(description="checked out", color="warning")
        elif event == Event.ALARM:
            return dict(description="alarm triggered", color="danger")
        elif event == Event.REGISTERED:
            return dict(description="card registered", color="")
        elif event == Event.BOOT:
            return dict(description="system restarted", color="info")
        else:
            return dict(description="", color="")
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
    elif action == 'r':
        event = Event.REGISTERED
    elif action == 'b':
        event = Event.BOOT
    else:
        abort(400, 'invalid action')
    timestamp = datetime.now() - timedelta(seconds=3) # account for the 3 second delay
    connection = get_db_connection()
    cur = connection.cursor()
    cur.execute('insert into log values(?, ?, ?)', (rfid, event, timestamp))
    cur.close()
    connection.commit()
    return 'OK\r\n'

if __name__ == '__main__':
    app.run('0.0.0.0',80)
