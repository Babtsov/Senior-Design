from bottle import *
import datetime


class TableEntry:
    def __init__(self, rfid, check_in='', check_out='', status='warning'):
        self.RFID = rfid
        self.status = status
        self.check_in = check_in
        self.check_out = check_out


table = [
    TableEntry(4950384950,"05:41:41 AM Sat Nov 12, 2016","05:41:54 AM Sat Nov 12, 2016", "success"),
    TableEntry(4950384950,"05:41:41 AM Sat Nov 12, 2016","05:41:54 AM Sat Nov 12, 2016", "success"),
    TableEntry(4950384950,"05:41:41 AM Sat Nov 12, 2016","05:41:54 AM Sat Nov 12, 2016", "danger"),
    TableEntry(3857403840,"05:45:11 AM Sat Nov 12, 2016","05:50:58 AM Sat Nov 12, 2016", "success"),
]

@route('/')
def main_page():
    return template('table', entries=table)


@route('/add/<id>/<action>')
def add_entry(id, action):
    entry = TableEntry(id)
    if action == 'i':
        entry.check_in = datetime.datetime.now().strftime("%I:%M:%S %p %a %b %d, %Y")
    elif action == 'o':
        entry.check_out = datetime.datetime.now().strftime("%I:%M:%S %p %a %b %d, %Y")
    else:
        abort(400,"Invalid action. Use i for check-in and o for check-out")
    table.append(entry)
    return 'OK'

run(host='0.0.0.0', port=80)


