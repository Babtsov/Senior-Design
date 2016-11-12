<!DOCTYPE html>
<html lang="en">
<head>
  <title>PharmaTracker Logger</title>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/css/bootstrap.min.css">
  <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.1.1/jquery.min.js"></script>
  <script src="https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/js/bootstrap.min.js"></script>
</head>
<body>

<div class="container">
  <h2>PharmaTracker Logger</h2>
  <p>Each event detected by the PharmaTracker system is recorded in the table below.</p>
  <table class="table table-hover">
    <thead>
      <tr>
        <th>Medicine ID</th>
        <th>Event</th>
        <th>Timestamp</th>
      </tr>
    </thead>
    <tbody>
    %for entry in entries:
        <tr class = "{{entry.status}}">
            <td>{{entry.RFID}}</td>
            <td>{{entry.event}}</td>
            <td>{{entry.timestamp}}</td>
        </tr>
    %end
    </tbody>
  </table>
</div>

</body>
</html>
