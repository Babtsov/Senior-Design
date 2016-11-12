<!DOCTYPE html>
<html lang="en">
<head>
  <title>PharmaTracker</title>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/css/bootstrap.min.css">
  <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.1.1/jquery.min.js"></script>
  <script src="https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/js/bootstrap.min.js"></script>
</head>
<body>

<div class="container">
  <h2>PharmaTracker</h2>
  <p>Below you'll find the check-in and check-out times of all the medicines currently attached to an RFID tag. Red columns indicate a medicine which was checked out more than allowed.</p>
  <table class="table table-hover">
    <thead>
      <tr>
        <th>Medicine ID</th>
        <th>Check-in Time</th>
        <th>Check-out Time</th>
      </tr>
    </thead>
    <tbody>
    %for entry in entries:
        <tr class = "{{entry.status}}">
            <td>{{entry.RFID}}</td>
            <td>{{entry.check_in}}</td>
            <td>{{entry.check_out}}</td>
        </tr>
    %end
    </tbody>
  </table>
</div>

</body>
</html>

