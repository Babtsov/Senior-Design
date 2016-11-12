from bottle import *
import datetime


class TableEntry:
    def __init__(self, rfid, action, timestamp, status = 'info'):
        self.RFID = rfid
        self.event = action
        self.timestamp = timestamp
        self.status = status


table = [
    TableEntry(4950384950,"checked out", "05:41:41 AM Sat Nov 12, 2016", "warning"),
    TableEntry(4950384950,"checked in", "05:41:41 AM Sat Nov 12, 2016", "success"),
    TableEntry(4950384950,"checked in", "05:41:41 AM Sat Nov 12, 2016", "success"),
    TableEntry(3857403840,"checked out", "05:45:11 AM Sat Nov 12, 2016", "warning"),
    TableEntry(3857403840,"alarm triggered", "05:45:11 AM Sat Nov 12, 2016", "danger"),
    TableEntry(3857403840,"checked out", "05:45:11 AM Sat Nov 12, 2016", "warning"),
    TableEntry(3857403840,"checked out", "05:45:11 AM Sat Nov 12, 2016", "warning"),
    TableEntry(1318475940,"checked out", "05:45:11 AM Sat Nov 12, 2016", "warning"),
    TableEntry(1318475940,"checked in", "05:41:41 AM Sat Nov 12, 2016", "success")
]


@route('/')
def main_page():
    return template('table', entries=table)


@route('/add/<id>/<action>')
def add_entry(id, action):
    if action == 'i':
        timestamp = datetime.datetime.now().strftime("%I:%M:%S %p %a %b %d, %Y")
        table.append(TableEntry(id, "check in", timestamp, "success"))
    elif action == 'o':
        timestamp = datetime.datetime.now().strftime("%I:%M:%S %p %a %b %d, %Y")
        table.append(TableEntry(id, "check out", timestamp, "warning"))
    elif action == 'a':
        timestamp = datetime.datetime.now().strftime("%I:%M:%S %p %a %b %d, %Y")
        table.append(TableEntry(id, "alarm triggered", timestamp, "danger"))
    else:
        abort(400,"Invalid action. Use i for check-in and o for check-out")

    return 'OK\r\n'

run(host='0.0.0.0', port=80)


