#/bin/bash/

for i in `seq 1 10`;
do
        ping -c 1 10.0.0.8
        ping -c 1 10.0.0.3
        ping -c 1 10.0.0.4
        ping -c 1 10.0.0.6
        ping -c 1 10.0.0.7
        ping -c 1 10.0.0.5
done

/sbin/m5 exit
