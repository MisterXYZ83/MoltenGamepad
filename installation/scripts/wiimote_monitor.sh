#!/bin/bash

#pistole wiimote
wm1="00:24:F3:E9:D7:17"
wm2="00:1C:BE:45:17:3A"

#ciclo di controllo
while [ true ]
do

  #stato wm1
  wm1_status=$(bluetoothctl info ${wm1})
  wm2_status=$(bluetoothctl info ${wm2})

#  echo $wm1_status
#  echo $wm2_status

  if [[ $wm1_status == *"Connected: no"* ]];
  then
    #connetto
    bluetoothctl connect $wm1
  fi

  if [[ $wm2_status == *"Connected: no"* ]];
  then
    #connetto
    bluetoothctl connect $wm2
  fi

  #pausa
  sleep 5

done
