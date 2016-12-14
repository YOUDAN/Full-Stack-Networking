


Full Stack Networking
=====================


Overview
--------

In this project, a full networking stack using the C socket interface is impelemented. First, the socket level calls are done in order to form point to point connections. Then the flooding (gossip) mechanism implemented is used to flood messages to all other nodes in the network. Finally, on top of the link-state protocol, a message sending interface is developed that will allow nodes to send
messages using the shortest path.

Note that you are writing this using IP, so nodes do not necessarily
have to exist on the same computer. You can
actually communicate with your friends' implementations!


The Details
-----------

There are several distinct components to this project:
    

1. You are able to connect two machines together using the connect command, which has the form:
 	> C192.168.2.248:54292
        
    This will connect your node to another node with IP address 
        192.168.2.248 and port 54292. You can also use domain names 
        instead of IP addresses in this command.
    
2.  You are able to gossip messages to each
    other. Gossip messages have the form:
            
    > G&lt;src_addr:port&gt;/&lt;counter&gt;/&lt;payload&gt;\n

   
3.  Finally, you can use a link-state routing algorithm to send message
new 'send' message. 
    > S&lt;dst_addr:port&gt;/&lt;TTL&gt;/&lt;payload&gt;\n

    

Rules of the Road
-----------------

In terminal, run `gcc *.c`. After that,
each run of `./a.out` should open up a new node that follows our
specification above.


Acknowledge
----------

Cornell CS 4410 Operating Systems Fall 2016

