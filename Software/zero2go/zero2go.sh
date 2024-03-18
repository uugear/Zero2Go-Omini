#!/bin/bash
# file: zero2go.sh
#

# include utilities script in same directory
my_dir="`dirname \"$0\"`"
my_dir="`( cd \"$my_dir\" && pwd )`"
if [ -z "$my_dir" ] ; then
  exit 1
fi
. "$my_dir/utilities.sh"
. "$my_dir/gpio-util.sh"

# take the ownership of the log file
sudo chown $USER:$USER $my_dir/zero2go.log

print_status()
{
  tput bold
  tput setaf 8
  tput rev
  echo ' Status                                                              '
  echo ''
  tput sgr0

  local va=$(read_channel_A)
  local vb=$(read_channel_B)
  local vc=$(read_channel_C)
  if (( $(awk "BEGIN {print ($va > $vb && $va > $vc)}") )); then
    tput setaf 2
  else
    tput setaf 8
	fi
	tput bold
	printf 'Channel-A: %.2fV   ' $va
	tput sgr0
  if (( $(awk "BEGIN {print ($vb > $va && $vb > $vc)}") )); then
    tput setaf 2
  else
    tput setaf 8
	fi
	tput bold
	printf 'Channel-B: %.2fV   ' $vb
	tput sgr0
  if (( $(awk "BEGIN {print ($vc > $va && $vc > $vb)}") )); then
    tput setaf 2
  else
    tput setaf 8
	fi
	tput bold
	printf 'Channel-C: %.2fV   \n' $vc
	tput sgr0

  tput setaf 2
  tput bold
  echo ''
  local mode=$(i2c_read 0x01 $I2C_SLAVE_ADDRESS $I2C_BULK_BOOST)
  local wm=''
  if [ $mode == '0x01' ]; then
    echo 'Working Mode: Step-Down'
    wm='Step-Down'
  else
    echo 'Working Mode: Step-Up'
    wm='Step-Up'
  fi
  tput sgr0
  log2file "Va=$va, Vb=$vb, Vc=$vc, mode=$wm"
}

print_menu()
{
  local default=$(i2c_read 0x01 $I2C_SLAVE_ADDRESS $I2C_CONF_DEFAULT_ON)
  if [ $default == '0x01' ]; then
    default='Default On'
  else
	  default='Default Off'
  fi
  
  local blink=$(i2c_read 0x01 $I2C_SLAVE_ADDRESS $I2C_CONF_BLINK_INTERVAL)
  if [ $blink == '0x09' ]; then
    blink=8
  elif [ $blink == '0x07' ]; then
	  blink=2
	elif [ $blink == '0x06' ]; then
	  blink=1
	else
	  blink=4
  fi
  
  local lowv=$(i2c_read 0x01 $I2C_SLAVE_ADDRESS $I2C_CONF_LOW_VOLTAGE)
  if [ $(($lowv)) == 255 ]; then
    lowv='Disabled'
	else
	  lowv=$(calc $(($lowv))/10)
	  lowv+='V'
  fi
  
  local recv=$(i2c_read 0x01 $I2C_SLAVE_ADDRESS $I2C_CONF_RECOVERY_VOLTAGE)
  if [ $(($recv)) == 255 ]; then
    recv='Disabled'
	else
    recv=$(calc $(($recv))/10)
	  recv+='V'
  fi
  
  local bulkawayson=$(i2c_read 0x01 $I2C_SLAVE_ADDRESS $I2C_CONF_BULK_ALWAYS_ON)
  if [ $bulkawayson == '0x01' ]; then
    bulkawayson='Yes'
  else
    bulkawayson='No'
  fi
  
  local powercutdelay=$(i2c_read 0x01 $I2C_SLAVE_ADDRESS $I2C_CONF_POWER_CUT_DELAY)
  powercutdelay=$(calc $(($powercutdelay))/10)
  
  tput bold
  tput setaf 8
  tput rev
  echo ' Menu                                                                '
  echo ''
  tput sgr0
  
  printf '1. Set default state when power is connected: [%s]\n' "$default"
  printf '2. Set blinking interval when standing by: [%d Seconds]\n' "$blink"
  printf '3. Set delay between shutdown and cutting power: [%.1f Seconds]\n' "$powercutdelay"
  printf '4. Set low voltage threshold: [%s]\n' "$lowv"
  printf '5. Set recovery voltage threshold: [%s]\n' "$recv"
  printf '6. Set step-down engine always-on: [%s]\n' "$bulkawayson"
  printf '7. Exit\n\n'
  
  log2file "$default, blink=$blink, power_cut=$powercutdelay, low_voltage=$lowv, recovery_voltage=$recv, bulk_always_on=$bulkawayson"
}

set_default_state()
{
  read -p 'Input new default state (1 or 0: 1=On, 0=Off): ' state
  case $state in
    0) i2c_write 0x01 $I2C_SLAVE_ADDRESS $I2C_CONF_DEFAULT_ON 0x00 && log 'Set to "Default Off"!' && sleep 2;;
    1) i2c_write 0x01 $I2C_SLAVE_ADDRESS $I2C_CONF_DEFAULT_ON 0x01 && log 'Set to "Default On"!' && sleep 2;;
    *) echo 'Please input 1 or 0 ...' && sleep 2;;
  esac
}

set_blink_interval()
{
	read -p 'Input new blink interval (1,2,4 or 8: value in seconds): ' interval
  case $interval in
    1) i2c_write 0x01 $I2C_SLAVE_ADDRESS $I2C_CONF_BLINK_INTERVAL 0x06 && log 'Blink interval set to 1 second!' && sleep 2;;
    2) i2c_write 0x01 $I2C_SLAVE_ADDRESS $I2C_CONF_BLINK_INTERVAL 0x07 && log 'Blink interval set to 2 seconds!' && sleep 2;;
    4) i2c_write 0x01 $I2C_SLAVE_ADDRESS $I2C_CONF_BLINK_INTERVAL 0x08 && log 'Blink interval set to 4 seconds!' && sleep 2;;
    8) i2c_write 0x01 $I2C_SLAVE_ADDRESS $I2C_CONF_BLINK_INTERVAL 0x09 && log 'Blink interval set to 8 seconds!' && sleep 2;;
    *) echo 'Please input 1,2,4 or 8 ...' && sleep 2;;
  esac
}

set_cut_power_delay()
{
  read -p 'Input new delay (0.0~8.0: value in seconds): ' delay
  if (( $(awk "BEGIN {print ($delay >= 0 && $delay <= 8.0)}") )); then
    local d=$(calc $delay*10)
    i2c_write 0x01 $I2C_SLAVE_ADDRESS $I2C_CONF_POWER_CUT_DELAY ${d%.*}
    log "Power cut delay set to $delay seconds!"
  else
    echo 'Please input from 0.0 to 8.0 ...'
  fi
  sleep 2
}

set_low_voltage_threshold()
{
  read -p 'Input low voltage (2.0~25.0: value in volts, 0=Disabled): ' threshold
  if (( $(awk "BEGIN {print ($threshold >= 2.0 && $threshold <= 25.0)}") )); then
    local t=$(calc $threshold*10)
    i2c_write 0x01 $I2C_SLAVE_ADDRESS $I2C_CONF_LOW_VOLTAGE ${t%.*}
    local ts=$(printf 'Low voltage threshold set to %.1fV!\n' $threshold)
    log "$ts"
  elif (( $(awk "BEGIN {print ($threshold == 0)}") )); then
    i2c_write 0x01 $I2C_SLAVE_ADDRESS $I2C_CONF_LOW_VOLTAGE 0xFF
    log 'Disabled low voltage threshold!'
  else
    echo 'Please input from 2.0 to 25.0 ...'
  fi
  sleep 2
}

set_recovery_voltage_threshold()
{
  read -p 'Input recovery voltage (2.0~25.0: value in volts, 0=Disabled): ' threshold
  if (( $(awk "BEGIN {print ($threshold >= 2.0 && $threshold <= 25.0)}") )); then
    local t=$(calc $threshold*10)
    i2c_write 0x01 $I2C_SLAVE_ADDRESS $I2C_CONF_RECOVERY_VOLTAGE ${t%.*}
    local ts=$(printf 'Recovery voltage threshold set to %.1fV!\n' $threshold)
    log "$ts"
  elif (( $(awk "BEGIN {print ($threshold == 0)}") )); then
    i2c_write 0x01 $I2C_SLAVE_ADDRESS $I2C_CONF_RECOVERY_VOLTAGE 0xFF
    log 'Disabled recovery voltage threshold!'
  else
    echo 'Please input from 2.0 to 25.0 ...'
  fi
  sleep 2
}

set_bulk_always_on()
{
  read -p 'Keep step-down engine alive even in step-up mode? (1=Yes, 0=No) ' state
  case $state in
    0) i2c_write 0x01 $I2C_SLAVE_ADDRESS $I2C_CONF_BULK_ALWAYS_ON 0x00 && log 'Step-down engine is now on-demand!' && sleep 2;;
    1) i2c_write 0x01 $I2C_SLAVE_ADDRESS $I2C_CONF_BULK_ALWAYS_ON 0x01 && log 'Step-down engine is now always-on!' && sleep 2;;
    *) echo 'Please input 1 or 0 ...' && sleep 2;;
  esac
}

# output and refresh the interface
log2file 'Zero2Go Omini console (v1.53) initialized...'

if one_wire_confliction ; then
	echo ''
	log 'Confliction detected:'
	log "1-Wire interface is enabled on GPIO-$HALT_PIN, which is also used by Zero2Go Omini."
	log 'You may solve this confliction by moving 1-Wire interface to another GPIO pin.'
	echo ''
	exit
fi

while true; do

tput clear

tput cup 1 20
tput setaf 4
tput bold
echo 'ZERO2GO OMINI CONSOLE'
tput sgr0
tput cup 2 16
tput setaf 8
echo 'v1.51 by Dun Cat B.V. (UUGear)'
tput sgr0

tput cup 4 0
print_status

tput cup 10 0
print_menu

read -t 5 -p 'Enter your choice (1~7) ' action
case $action in
  1) set_default_state;;
  2) set_blink_interval;;
  3) set_cut_power_delay;;
  4) set_low_voltage_threshold;;
  5) set_recovery_voltage_threshold;;
  6) set_bulk_always_on;;
  7) log2file 'Exiting Zero2Go Omini console...'; exit;;
  *) ;;
esac

echo ''

done
