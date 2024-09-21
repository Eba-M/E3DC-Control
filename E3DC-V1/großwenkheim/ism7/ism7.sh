#!/bin/bash
cd /home/pi/ism7
while true;
 do
echo "ism7mqtt  wird gestartet"
echo $(date +%H%M)
./ism7mqtt -m 192.168.178.54 -i 192.168.178.89 -p Wenki100
sleep 10
done

