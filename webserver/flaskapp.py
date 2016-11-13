from flask import Flask, render_template, abort
from datetime import datetime


class Event:
    CHECK_IN, CHECK_OUT, ALARM = range(3)


class TableEntry:
    def __init__(self, rfid, action, timestamp):
        self.RFID = rfid
        self.event = action
        self.timestamp = timestamp

    def get_event_info(self):
        if self.event == Event.CHECK_IN:
            return dict(description="checked in", color="success")
        elif self.event == Event.CHECK_OUT:
            return dict(description="checked out", color="warning")
        elif self.event == Event.ALARM:
            return dict(description="alarm triggered", color="danger")
        else:
            return dict(description="", color="info")

table = [
    TableEntry(4950384950, Event.CHECK_OUT, datetime(2016, 4, 30, 3, 1, 38)),
    TableEntry(4950384950, Event.CHECK_IN, datetime(2016, 4, 30, 3, 11, 38)),
    TableEntry(4950384950, Event.CHECK_IN, datetime(2016, 4, 30, 3, 1, 1)),
    TableEntry(3857403840, Event.CHECK_OUT, datetime(2016, 5, 30, 3, 1, 38)),
    TableEntry(3857403840, Event.ALARM, datetime(2016, 4, 6, 3, 1, 38)),
    TableEntry(3857403840, Event.CHECK_OUT, datetime(2016, 7, 30, 3, 1, 38)),
    TableEntry(3857403840, Event.ALARM, datetime(2016, 4, 8, 3, 1, 38)),
    TableEntry(1318475940, Event.CHECK_OUT, datetime(2016, 8, 30, 3, 1, 38)),
    TableEntry(1318475940, Event.CHECK_IN, datetime(2016, 9, 30, 3, 1, 38))
]

app = Flask(__name__)


@app.route('/')
def main_page():
    return render_template('table.html', entries=table)


@app.route('/add/<rfid>/<action>')
def add_entry(rfid, action):
    timestamp = datetime.now()
    if action == 'i':
        table.append(TableEntry(rfid, Event.CHECK_IN, timestamp))
    elif action == 'o':
        table.append(TableEntry(rfid, Event.CHECK_OUT, timestamp))
    elif action == 'a':
        table.append(TableEntry(rfid, Event.ALARM, timestamp))
    else:
        abort(400, 'invalid action')
    return 'OK\r\n'

if __name__ == '__main__':
    app.run()
