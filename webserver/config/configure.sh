#!/bin/bash 
# Script to automatically configure the PharmaTracker server app
apt-get update
apt-get install apache2
apt-get install libapache2-mod-wsgi
apt-get install python-pip
pip install flask
ln -sT ~/Senior-Design/webserver /var/www/html/flaskapp
cp apache.conf /etc/apache2/sites-enabled/000-default.conf
mkdir /data
python create_database.py
chown www-data /data /data/logs.db
apachectl restart
echo "done configuting system"
