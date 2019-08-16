[ -z $BASH ] && { exec bash "$0" "$@" || exit; }
#!/bin/bash
# file: installZero2Go.sh
#
# This script will install required software for Zero2Go Omini.
# It is recommended to run it in your account's home directory.
#

# check if sudo is used
if [ "$(id -u)" != 0 ]; then
  echo 'Sorry, you need to run this script with sudo'
  exit 1
fi

# target directory
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )/zero2go"

# error counter
ERR=0

echo '================================================================================'
echo '|                                                                              |'
echo '|              Zero2Go-Omini Software Installation Script                      |'
echo '|                                                                              |'
echo '================================================================================'

# enable I2C on Raspberry Pi
echo '>>> Enable I2C'
if grep -q 'i2c-bcm2708' /etc/modules; then
  echo 'Seems i2c-bcm2708 module already exists, skip this step.'
else
  echo 'i2c-bcm2708' >> /etc/modules
fi
if grep -q 'i2c-dev' /etc/modules; then
  echo 'Seems i2c-dev module already exists, skip this step.'
else
  echo 'i2c-dev' >> /etc/modules
fi
if grep -q 'dtparam=i2c1=on' /boot/config.txt; then
  echo 'Seems i2c1 parameter already set, skip this step.'
else
  echo 'dtparam=i2c1=on' >> /boot/config.txt
fi
if grep -q 'dtparam=i2c_arm=on' /boot/config.txt; then
  echo 'Seems i2c_arm parameter already set, skip this step.'
else
  echo 'dtparam=i2c_arm=on' >> /boot/config.txt
fi
if grep -q 'dtoverlay=pi3-miniuart-bt' /boot/config.txt; then
  echo 'Seems setting Pi3 Bluetooth to use mini-UART is done already, skip this step.'
else
  echo 'dtoverlay=pi3-miniuart-bt' >> /boot/config.txt
fi
if grep -q 'core_freq=250' /boot/config.txt; then
  echo 'Seems the frequency of GPU processor core is set to 250MHz already, skip this step.'
else
  echo 'core_freq=250' >> /boot/config.txt
fi
if [ -f /etc/modprobe.d/raspi-blacklist.conf ]; then
  sed -i 's/^blacklist spi-bcm2708/#blacklist spi-bcm2708/' /etc/modprobe.d/raspi-blacklist.conf
  sed -i 's/^blacklist i2c-bcm2708/#blacklist i2c-bcm2708/' /etc/modprobe.d/raspi-blacklist.conf
else
  echo 'File raspi-blacklist.conf does not exist, skip this step.'
fi

# install i2c-tools
echo '>>> Install i2c-tools'
if hash i2cget 2>/dev/null; then
  echo 'Seems i2c-tools is installed already, skip this step.'
else
  apt-get install -y i2c-tools || ((ERR++))
fi

# check if it is Jessie or above
osInfo=$(cat /etc/os-release)
if [[ $osInfo == *"jessie"* || $osInfo == *"stretch"* || $osInfo == *"buster"* ]] ; then
  isJessieOrAbove=true
else
  isJessieOrAbove=false
fi

# install wiringPi
if [ $ERR -eq 0 ]; then
  echo '>>> Install wiringPi'
  if hash gpio 2>/dev/null; then
    echo 'Seems wiringPi is installed already, skip this step.'
  else
    if $isJessieOrAbove ; then
      apt-get install -y wiringpi || ((ERR++))
    else
      if hash git 2>/dev/null; then
        echo "Git has been installed already..."
      else
        echo "Git is missing, install it now..."
        apt-get install -y git || ((ERR++))
      fi
      if [ $ERR -eq 0 ]; then
        git clone git://git.drogon.net/wiringPi || ((ERR++))
        cd wiringPi
        ./build
        cd ..
      fi
    fi
  fi
fi

# install Zero2Go Omini
if [ $ERR -eq 0 ]; then
  echo '>>> Install zero2go'
  if [ -d "zero2go" ]; then
    echo 'Seems zero2go is installed already, skip this step.'
  else
    wget http://www.uugear.com/repo/Zero2GoOmini/LATEST -O zero2go.zip || ((ERR++))
    unzip zero2go.zip -d zero2go || ((ERR++))
    cd zero2go
    chmod +x zero2go.sh
    chmod +x daemon.sh
    sed -e "s#/home/pi/zero2go#$DIR#g" init.sh >/etc/init.d/zero2go_daemon
    chmod +x /etc/init.d/zero2go_daemon
    update-rc.d zero2go_daemon defaults
    cd ..
    chown -R pi:pi zero2go
    sleep 2
    rm zero2go.zip
  fi
fi

echo
if [ $ERR -eq 0 ]; then
  echo '>>> All done. Please reboot your Pi :-)'
else
  echo '>>> Something went wrong. Please check the messages above :-('
fi
