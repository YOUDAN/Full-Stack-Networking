CS 4410 Operating Systems Fall 2016
___

10-P3: Full Stack Networking
=====================

**Due**: Wednesday, November 2 @ 11:59 PM

Overview
--------

In this project, you will learn about how to build a full networking
stack using the C socket interface. You will first implement the
socket level calls in order to form point to point connections.  In
the starter code, there already exists a flooding (gossip) mechanism
that allows you to flood messages to all other nodes in the
network. Recall that gossip protocols call for a receiver to broadcast 
whatever data it received to all of its neighbors. 
Using this mechanism, you will have to implement a link-state
routing protocol like you learned in 
[class](http://www.cs.cornell.edu/Courses/cs4410/2016fa/slides/09-networking-3.pdf). 
Finally, on top of the link-state protocol, you will
implement a message sending interface that will allow nodes to send
messages using the shortest path.

Note that you are writing this using IP, so nodes do not necessarily
have to exist on the same computer. If written correctly, you can
actually communicate with your friends' (and the TAs') implementations
of this project!

One note: while we give you a good amount of skeleton code, you can
(**and will probably need to**) add or change some of the given
code. **BE SURE TO READ OVER AND UNDERSTAND ALL OF THE SKELETON CODE
BEFORE STARTING**. It is important to understand what we have given
you in order for you to figure out what we have already implemented
for you and what you actually need to implement/where to implement it.

The Details
-----------

There are several distinct components to this project:
    
1. For the first part, you will be working in `connect.c` and
utilizing several standard socket API calls, which (along with their
respective options) may be found in the man pages on your terminal or
online. In the `main()` method, you will have to insert code where
indicated that does the following:
        
    * Create a non-blocking TCP socket using the `socket()` and
      `fcntl()` system calls.
    * Set the `SO_REUSEADDR` option on the socket using the
      `setsocketopt()`
    * Use `bind()` to bind the socket to `bind_port`
    * In addition, you will have to make a non-blocking socket in
      `try_connect` and `server_handler` where indicated. After this
      part, you should be able to compile and run your code.  At the
      prompt, you should be able to connect two machines together
      using the connect command, which has the form:
        
        > C192.168.2.248:54292
        
        This will connect your node to another node with IP address 
        192.168.2.248 and port 54292. You can also use domain names 
        instead of IP addresses in this command.
    
    In addition, you should be able to gossip messages to each
    other. Gossip messages have the form:
            
    > G&lt;src_addr:port&gt;/&lt;counter&gt;/&lt;payload&gt;\n

    For now, you will be able to put arbitrary payloads in them. You
    may have to put a print statement in `gossip.c` in order to see
    the gossip protocol working. Later on, you **will** be changing
    the gossip protocol so that only link-state messages will be
    flooded. The `counter` field in the gossip message is simply there
    for versioning: a higher counter from the same source means that
    the message has been updated. **A node will only keep track of the
    most recent gossip**.


2. This next part will be done in `link_state.c`. Here, you will first
implement [Dijkstra's shortest path
algorithm](http://www.cs.cornell.edu/courses/cs2110/2016sp/L20-GraphsIII/cs2110ShortestPath.pdf). The
function signature and some utility functions are given here.

3. Now, you must implement a flooding mechanism that will broadcast
your active connections every time your connection state changes. This
will use the gossip mechanism mentioned in part 1. A gossip message
will have the form:

    > G&lt;src_addr:port&gt;/&lt;counter&gt;/&lt;payload&gt;\n

    where the payload is

    > ";&lt;addr1:port1&gt;;&lt;addr2:port2&gt;;&lt;addr3:port3&gt;...\n"

    Essentially, the payload is a list of hosts to which you are
    connected. From this point onwards, your gossip messages **must**
    be of this format in order to be interoperable with other
    solutions.

    The simplest way to implement the flooding mechanism is by simply
    broadcasting the message you have received to everyone you
    know. However, these "broadcast storms" are not good, so we make
    two optimizations.

    1. Don't tell the person who sent you the message.
    2. Don't spread around messages you have seen already.

  Note that node names are strings. However, we give you ways to convert 
  from a string version of a node name to a `sockaddr_in` and back in 
  `link_state.c`.

4. Finally, you must implement a link-state routing algorithm and a
new 'send' message. The send message has a similar format as the
gossip format. The main difference is that the address belongs to the
destination, not the source, and the TTL is the maximum number of hops
for this packet.

    > S&lt;dst_addr:port&gt;/&lt;TTL&gt;/&lt;payload&gt;\n

    Remember, the link-state algorithm recomputes the shortest path
    every time it receives a new routing update, so the message will
    be sent through the current shortest path. When operating on a
    send message, you must do the following:

    1. If the send message belongs to you, then print out the payload.
    2. If the send message belongs to someone else, then decrement the
    TTL by 1. If TTL == 0 then drop the message. Otherwise, forward it
    according to the shortest path.

    Note: Though it is not required, the idea of routing tables as seen in 
    Problem Solving Sessions and lecture might be helpful for this part.

Rules of the Road
-----------------

1. You only get one header file for your core project: `global.h`. You
are allowed to modify it at will. Note that although it is typically
bad practice to expose struct fields in a header file, it is ok for
this project.
2. When we test your code, we will simply run `gcc *.c`. After that,
each run of `./a.out` should open up a new node that follows our
specification above.

Submission
----------

Push your code to Github. You can (and realistically should) push your
code as many times as you want, such as when you have made a
significant change or improvement. Whatever code is in the repo at
midnight the night that the assignment is due will be what is graded.

Final Word
----------

Start early and have fun.

