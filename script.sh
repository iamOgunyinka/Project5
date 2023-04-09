#/usr/bin/bash

apt update && apt -y upgrade
cd ~
mkdir -p ~/documents/
mkdir -p ~/documents/boost_dir
mkdir -p ~/documents/nginx
mkdir -p ~/documents/wudi_logs
mkdir -p ~/documents/files

echo ======================================
echo Install all the necessary tools needed
echo ======================================
apt -y install python3-dev git cmake python3-pip libssl-dev unixodbc-dev nginx python-is-python3 gunicorn supervisor mysql-server
apt -y install libmysqlclient-dev pkg-config openssl libzip4 lzip zip dos2unix unixodbc zlib1g-dev libmysqlcppconn-dev
wget https://boostorg.jfrog.io/artifactory/main/release/1.81.0/source/boost_1_81_0.tar.gz -O ~/documents/boost_181.tar.gz
tar -xzvf ~/documents/boost_181.tar.gz -C ~/documents/boost_dir
export BOOST_ROOT="/root/documents/boost_dir/boost_1_81_0/"
pip3 install flask flask_migrate mysqlclient
mysql_upgrade -u root -p

echo ===================================
echo Create user and database needed
echo ===================================
mysql -u root<<EOFMYSQL
CREATE USER 'iamOgunyinka'@'localhost' IDENTIFIED BY '12345678zxcvbnm()';
create database wudi_db_develop;
GRANT ALL ON wudi_db_develop.* TO 'iamOgunyinka'@'localhost';
EOFMYSQL

git clone https://github.com/iamOgunyinka/Project5.git
cd Project5
git checkout server
cd server-3.1/

echo ===================================================
echo Create and use a swapfile before compiling the code
echo ===================================================
sudo dd if=/dev/zero of=/swapfile bs=8196 count=1048576
sudo mkswap /swapfile
sudo chmod 0600 /swapfile
sudo swapon /swapfile

echo ===================================
echo Run CMake and Compile the C++ code
echo ===================================
cmake . && make -j2
sudo swapoff /swapfile
sudo rm /swapfile

wget http://nz2.archive.ubuntu.com/ubuntu/pool/main/o/openssl/libssl1.1_1.1.1f-1ubuntu2.17_amd64.deb -O ~/documents/libssl_amd64.deb
apt install -y ~/documents/libssl_amd64.deb

echo ===============================
echo Create the database tables
echo ===============================
cd ../scripts/
python ./run.py
dos2unix config/database.ini

echo ======================================
echo Copy the necessary configuration files
echo ======================================
cd ..
tar -xzvf files.tar.gz -C ~/documents/files
sudo mkdir -p /usr/lib/x86_64-linux-gnu/odbc/
sudo cp ~/documents/files/lib*.so /usr/lib/x86_64-linux-gnu/odbc/
sudo cp ~/documents/files/*.ini /etc/
sudo cp ~/documents/files/supervisord.conf /etc/supervisor/
sudo cp ~/documents/files/nginx.conf /etc/nginx/

echo =======================================================
echo If starting supervisor fails, you may need to edit the
echo "'/etc/supervisor/supervisor.conf' file to point to the"
echo location of the new executables and then run
echo "'service supervisor start' to run the services."
echo =======================================================

supervisorctl shutdown
nginx -s stop
service supervisor start
supervisorctl status wudi_server

echo ================================
echo start the reverse proxy server
echo ================================

service nginx start
