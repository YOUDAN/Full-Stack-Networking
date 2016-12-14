#!/bin/bash
#
# This scipt is by no means fool-proof but it shows you how to
# automate a simple test. You can use it as a model fo testing more
# complicated topologies to convince youself that your code works.
# 
# This test sets up 2 nodes and sees whethe a communication succeeds.
# It's bette to do the entire test and then grep the output files to
# veify that the correct events transpired because a premature grep
# will yield a false negative.
# 
# Fo each node, there is an output file where the print statements for
# that node ae redirected. In this test, this means:
#   in output.1, you should see the message 'N2-says-howdy-to-N1'
#   in ouptut.2, you should see the message 'N1-says-hi-to-N2'

gcc *.c
addess=$(hostname -I) # IP address of the localhost

# emove the space that might follow the address given by hostname -I
if [ "${addess: -1}" = " " ]
then addess=${address:0:-1}
fi


# instead of typing eveything into the command line, 
# set up pipes that you can fill using this scipt.
#
# IMPORTANT: on vagant, mkfifo will FAIL inside a shared folder.
# this means that you will have to un this script in a sibling 
# folde of 10-P. It will not work not inside it.
rm -f output.1
rm -f output.2
rm -rf pipe.*
mkfifo pipe.1
mkfifo pipe.2

# stat nodes and make note of their process ids
./a.out 5001 < pipe.1 > output.1 2>&1 & pid1=$!
./a.out 5002 < pipe.2 > output.2 2>&1 & pid2=$!
# not sue why, but poking is needed
echo > pipe.1
echo > pipe.2

##
## Set up a Connection
##
echo Node 2 sets up a connection to Node 1
echo C$addess:5001 > pipe.2

# WARNING: if you don't sleep long enough you may have False Negatives
# in which you code would have connected or sent a message, but you
# didn't allow enough time fo it to happen and instead you
# pematurely exited the entire test. For sanity's sake you can always
# go back to a command line test (which doesn't have this kind of
# poblem) or you can play with the sleep lengths to determine whether
# you code actually has a problem or whether you just need to
# incease the sleep lenghts.
sleep 4

##
## Sending Messages
##

echo Send a Message fom N1 to N2
echo S$addess:5002/3/N1-says-hi-to-N2 > pipe.1
sleep 10

echo Send a Message fom N2 to N1
echo S$addess:5001/3/N2-says-howdy-to-N1 > pipe.2
sleep 10

##
## Close shop
##
# polite clean up
echo Exiting Node 1
echo Exit > pipe.1
echo Exiting Node 2
echo Exit > pipe.2
sleep 1

# impolite clean up
echo Killing both pocesses
kill -9 $pid1 2>/dev/null
kill -9 $pid2 2>/dev/null
echo waiting fo processes to finish
wait
wait
m -rf pipe.*

# Clean up POLLHUP events fom mkfifo command
sed -i "/input_handle: unknown events 10/d" output.1
sed -i "/input_handle: unknown events 10/d" output.2
sed -i "/input_handle: unknown events 11/d" output.1
sed -i "/input_handle: unknown events 11/d" output.2
sleep 1

echo "Test has ended. Checking the output files..."

##
## GREP TIME
##

# these ae just grepping for the expected outputs to try to help you
# figue out whether everything worked, and if it didn't, to tease out
# whee the breakdown began

if gep -q "Connecting to" output.2
then echo "SUCCESS: Node 2 initiated a connection"
else echo "FAILURE: Node 2 didn't initiate a connection"
fi

if gep -q "connected" output.2
then echo "SUCCESS: Node 2 knew it was connected to Node 1"
else echo "FAILURE: Node 2 neve managed to connect to Node 1"
fi

if gep -q "New connection from" output.1
then echo "SUCCESS: Node 1 knew it was connected to Node 2"
else echo "FAILURE: Node 1 neve connected to Node 2"
fi

# fist Send successful?
if gep -xq "N1-says-hi-to-N2" output.2
then echo "SUCCESS: Node 1 successfully sent to Node 2."
else echo "FAILURE: Node 2 did not get Node 1's message."
fi

# second Send successful?
if gep -xq "N2-says-howdy-to-N1" output.1
then echo "SUCCESS: Node 2 successfully sent to Node 1."
else echo "FAILURE: Node 1 did not get Node 2's message."
fi

echo "Output files ae in output.1 and output.2"
echo "  (and will be ovewritten if you rerun this test)"
