#!/bin/bash
# file: utilities.sh
#
# This script provides some useful utility functions
#

export LC_ALL=en_GB.UTF-8

readonly I2C_SLAVE_ADDRESS=0x29

readonly I2C_CHANNEL_AI=1
readonly I2C_CHANNEL_AD=2
readonly I2C_CHANNEL_BI=3
readonly I2C_CHANNEL_BD=4
readonly I2C_CHANNEL_CI=5
readonly I2C_CHANNEL_CD=6
readonly I2C_BULK_BOOST=7
readonly I2C_LV_SHUTDOWN=8

readonly I2C_CONF_ADDRESS=9
readonly I2C_CONF_DEFAULT_ON=10
readonly I2C_CONF_BLINK_INTERVAL=11
readonly I2C_CONF_LOW_VOLTAGE=12
readonly I2C_CONF_BULK_ALWAYS_ON=13
readonly I2C_CONF_POWER_CUT_DELAY=14
readonly I2C_CONF_RECOVERY_VOLTAGE=15

# halt by GPIO-4 (BCM naming)
readonly HALT_PIN=4

# pull up GPIO-17 (BCM naming) to indicate system is up
readonly SYSUP_PIN=17


one_wire_confliction()
{
	if [[ $HALT_PIN -eq 4 ]]; then
		if grep -qe "^\s*dtoverlay=w1-gpio\s*$" /boot/config.txt; then
	  	return 0
		fi
		if grep -qe "^\s*dtoverlay=w1-gpio-pullup\s*$" /boot/config.txt; then
	  	return 0
		fi
	fi 
  if grep -qe "^\s*dtoverlay=w1-gpio,gpiopin=$HALT_PIN\s*$" /boot/config.txt; then
  	return 0
	fi
	if grep -qe "^\s*dtoverlay=w1-gpio-pullup,gpiopin=$HALT_PIN\s*$" /boot/config.txt; then
  	return 0
	fi
	return 1
}

zero2go_home="`dirname \"$0\"`"
zero2go_home="`( cd \"$zero2go_home\" && pwd )`"
log2file()
{
  local datetime=$(date +'[%Y-%m-%d %H:%M:%S]')
  local msg="$datetime $1"
  echo $msg >> $zero2go_home/zero2go.log
}

log()
{
  if [ $# -gt 1 ] ; then
    echo $2 "$1"
  else
    echo "$1"
  fi
  log2file "$1"
}

calc()
{
  awk "BEGIN { print $*}";
}

dec2hex()
{
  printf "0x%02x" $1
}

i2c_read()
{
  local retry=0
  if [ $# -gt 3 ] ; then
    retry=$4
  fi
  local result=$(i2cget -y $1 $2 $3)
  if [[ $result =~ ^0x[0-9a-fA-F]{2}$ ]] ; then
    echo $result;
  else
    retry=$(( $retry + 1 ))
    if [ $retry -eq 4 ] ; then
      log "I2C read $1 $2 $3 failed (result=$result), and no more retry."
    else
      sleep 1
      log2file "I2C read $1 $2 $3 failed (result=$result), retrying $retry ..."
      i2c_read $1 $2 $3 $retry
    fi
  fi
}

i2c_write()
{
  local retry=0
  if [ $# -gt 4 ] ; then
    retry=$5
  fi
  i2cset -y $1 $2 $3 $4
  local result=$(i2c_read $1 $2 $3)
  if [ "$result" != $(dec2hex "$4") ] ; then
    retry=$(( $retry + 1 ))
    if [ $retry -eq 4 ] ; then
      log "I2C write $1 $2 $3 $4 failed (result=$result), and no more retry."
    else
      sleep 1
      log2file "I2C write $1 $2 $3 $4 failed (result=$result), retrying $retry ..."
      i2c_write $1 $2 $3 $4 $retry
    fi
  fi
}

read_channel_A()
{
	local i=$(i2c_read 0x01 $I2C_SLAVE_ADDRESS $I2C_CHANNEL_AI)
	local d=$(i2c_read 0x01 $I2C_SLAVE_ADDRESS $I2C_CHANNEL_AD)
	calc $(($i))+$(($d))/100
}

read_channel_B()
{
	local i=$(i2c_read 0x01 $I2C_SLAVE_ADDRESS $I2C_CHANNEL_BI)
	local d=$(i2c_read 0x01 $I2C_SLAVE_ADDRESS $I2C_CHANNEL_BD)
	calc $(($i))+$(($d))/100
}

read_channel_C()
{
	local i=$(i2c_read 0x01 $I2C_SLAVE_ADDRESS $I2C_CHANNEL_CI)
	local d=$(i2c_read 0x01 $I2C_SLAVE_ADDRESS $I2C_CHANNEL_CD)
	calc $(($i))+$(($d))/100
}

get_os()
{
  echo $(hostnamectl | grep 'Operating System:' | sed 's/.*Operating System: //')
}

get_kernel()
{
  echo $(uname -sr)
}

get_arch()
{
  echo $(dpkg --print-architecture)
}
