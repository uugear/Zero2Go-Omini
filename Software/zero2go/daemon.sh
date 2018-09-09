#!/bin/bash
# file: daemon.sh
#
# This script should be auto started, to support Zero2Go Omini hardware
#

# halt by GPIO-4 (BCM naming)
readonly HALT_PIN=4

# pull up GPIO-17 (BCM naming) to indicate system is up
readonly SYSUP_PIN=17

# get current directory
cur_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# utilities
. "$cur_dir/utilities.sh"

log 'Zero2Go Omini daemon (v1.00) is started.'

# make sure the halt pin is input with internal pull up
gpio -g mode $HALT_PIN up
gpio -g mode $HALT_PIN in

# delay until GPIO pin state gets stable
counter=0
while [ $counter -lt 5 ]; do  # increase this value if it needs more time
  if [ $(gpio -g read $HALT_PIN) == '1' ] ; then
    counter=$(($counter+1))
  else
    counter=0
  fi
  sleep 1
done

# check if it is recovery from low-voltage shutdown
recovery=$(i2c_read 0x01 $I2C_SLAVE_ADDRESS $I2C_LV_SHUTDOWN)
if [ $recovery == '0x01' ]; then
  va=$(read_channel_A)
  vb=$(read_channel_B)
  vc=$(read_channel_C)
  log 'System recovered from previous low-voltage shutdown:'
  log "Va=$va, Vb=$vb, Vc=$vc"
fi

# indicates system is up
log "Send out the SYS_UP signal via GPIO-$SYSUP_PIN pin."
gpio -g mode $SYSUP_PIN out
gpio -g write $SYSUP_PIN 1
sleep 0.5
gpio -g write $SYSUP_PIN 0
gpio -g mode $SYSUP_PIN in

# wait for GPIO-4 (BCM naming) falling
gpio -g wfi $HALT_PIN falling

# light the red LED
gpio -g mode $SYSUP_PIN out
gpio -g write $SYSUP_PIN 1

# restore HALT_PIN
gpio -g mode $HALT_PIN in
gpio -g mode $HALT_PIN up

# check if it is because of low voltage
lvoff=$(i2c_read 0x01 $I2C_SLAVE_ADDRESS $I2C_LV_SHUTDOWN)
if [ $lvoff == '0x01' ]; then
  va=$(read_channel_A)
  vb=$(read_channel_B)
  vc=$(read_channel_C)
  log 'Turning off system because the input voltage is too low:'
  log "Va=$va, Vb=$vb, Vc=$vc"
else
  log "Turning off system because GPIO-$HALT_PIN pin is pulled down."
fi

# halt everything and shutdown
shutdown -h now
