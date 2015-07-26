#/bin/bash/
ping -c 1 10.0.0.3
ping -c 1 10.0.0.4
ping -c 1 10.0.0.5

echo "ping was successful"
/sbin/m5 exit
