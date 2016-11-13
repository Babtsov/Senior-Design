apt-get update
apt-get install apache2
apt-get install libapache2-mod-wsgi
apt-get install python-pip
pip install flask
ln -sT .. /var/www/html/flaskapp
cp apache.conf /etc/apache2/sites-enabled/000-default.conf
sudo apachectl restart
echo "done configuting apache"
