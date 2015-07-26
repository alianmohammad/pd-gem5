#!/bin/bash

#
# This is a tricky script to understand. When run in M5, it creates
# a checkpoint after Linux boot up, but before any benchmarks have
# been run. By playing around with environment variables, we can
# detect whether the checkpoint has been taken.
#  - If the checkpoint hasn't been taken, the script allows M5 to checkpoint the system,
# re-read this script into a new tmp file, and re-run it. On the
# second execution of this script (checkpoint has been taken), the
# environment variable is already set, so the script will exit the
# simulation
#  - When we restore the simulation from a checkpoint, we can
# specify a new script for M5 to execute in the full-system simulation,
# and it will be executed as if a checkpoint had just been taken.
#
# Author:
#   Joel Hestness, hestness@cs.utexas.edu
#   while at AMD Research and Advanced Development Lab
# Date:
#   10/5/2010
#
#
# Modified by:
# Lokesh Jindal, lokeshjindal15@cs.wisc.edu
# Mohammad Alian, alian@wisc.edu
#

################################################################################
# Tips:
# If restoring from a previous ckpt created using this script and 
# want to create a second ckpt using this script,
# make sure you rename RUNSCRIPT_VAR1_TUX0_ to a different variable that was not
# defined in the script used to create the first ckpt.
################################################################################

# Test if the RUNSCRIPT_VAR1_TUX4 environment variable is already set
echo "***** Start tux3 ckpt script! *****"
if [ "${RUNSCRIPT_VAR1_TUX4+set}" != set ]
then
	# Signal our future self that it's safe to continue
	echo "RUNSCRIPT_VAR1_TUX4 not set! So setting up network. Then reload"\
            "self, don't set up network and do this again n again n again n ..."
	export RUNSCRIPT_VAR1_TUX4=1
else
	echo "Else part - RUNSCRIPT_VAR1_TUX4 is set! So reload and execute!"
	echo "Else part - Loading new script..."
	/sbin/m5 readfile > /tmp/runscript1.sh
	chmod 755 /tmp/runscript1.sh

	# Execute the new runscript
	if [ -s /tmp/runscript1.sh ]
	then
		#/system/bin/sh /data/runscript1.sh
		echo "Else part - executing newly loaded script ...!"
		/bin/bash /tmp/runscript1.sh

	else
		echo "Else part - Script not specified."
		echo "Else part - Exiting..."
		/sbin/m5 exit
	fi
fi

echo "Setting up network ..."

ifconfig eth0 10.0.0.6
ifconfig lo up
ifconfig eth0 hw ether 00:90:00:00:00:05

################################################################################

#THIS IS WHERE EXECUTION BEGINS FROM AFTER RESTORING FROM CKPT
if [ "$RUNSCRIPT_VAR1_TUX4" -eq 1 ]
then

	# Signal our future self not to recurse infinitely
	export RUNSCRIPT_VAR1_TUX4=2
	echo "3. RUNSCRIPT_VAR1_TUX4 is $RUNSCRIPT_VAR1_TUX4"

	# Read the script for the checkpoint restored execution
	echo "Loading new script..."
	/sbin/m5 readfile > /tmp/runscript1.sh
	chmod 755 /tmp/runscript1.sh

	# Execute the new runscript
	if [ -s /tmp/runscript1.sh ]
	then
		#/system/bin/sh /data/runscript1.sh
		echo "executing newly loaded script ..."
		/bin/bash /tmp/runscript1.sh

	else
		echo "Script not specified. Dropping into shell..."
	fi

fi

echo "Fell through script. Exiting..."
/sbin/m5 exit
