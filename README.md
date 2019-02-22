0. What is it?
-------------

This is a server and client applications.
Server waiting for a data from clients, and store it in database.

1. Build
-------------

    $ mkdir build && cd build
    $ cmake ../
    $ make

2. Run
-------------

Run the server
    $ ./monitor/monitor

Run the client
    $ ./client/client

Run with `-help` argument to see how to use

3. Log configuration
-------------

Project uses zlog library. Config file located in `config`. 
Run `monitor` from the build dirictory or pass path to config in exec command
