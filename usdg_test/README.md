The device server USDG mode test tool
=====================================


USDG TCP mode test 
---------------------------------------
## 1. USDG TCP client mode

In `usdg_tcp_clt.c`, there are two thread working:

Thread 1  
```
do{ 
	open 1 connection;
	write(256 byte);
	recv(256 byte);
	close 1 connection ;
} while(1);
```
Thread 2
```
do{ 
	open 4 connection;
	recv(256 byte);
	close 4 connection;
} while(1);
```
Support:  
(1) normal mode test  
(2) data packing by character test  
(3) data packing by time test  
(4) data packing by data size test

**Compile** : make in gcc with c99

**Usage** : ./tcp_clt [-a/-i/-p/-c/-h/-s/-m] argumemt 	(-h for help)

			-a      IP address
    		-p      Data listening port 
			-s		Packing size (byte)
    		-i      Interval time (usec)
    		-c      End-character (hex)
            -m		enable multi-threaed
    		-h      For help

## 2. USDG TCP server mode

In`usdg_tcp_svr.c`, there are 18 threads working:

Thread 1
```
do{ 
	serial write(5 byte); 
} while(1);
```
Thread 2
```
do{ 
	serial read(10K byte); from TCP 
} while(1);
```
Thread 3 ~ 16
```
pthread_create(3~16);
do{
	recv(5 byte);
	if(now thread = 3/4/5.... in order){
		send(10K byte);
	}
}while(1);
pthread_exit(3/4/5....in order);

try to Keeps at least 15 thread alive
```

**Compile** : make in gcc with c99

**Usage** : ./tcp_svr [-c/-p/-b/-h] argument	(-h for help)

			-p		Port number
			-c		Com port path
			-b		Set baud rate
			-h		For help

USDG UDP mode test 
---------------------------------------

## 1. USDG UDP mode simple test

In `usdg_udp_simp.c`, there are 2 threads working:

Thread 1 (serial write thread)
```
open serial port
do{ 
	write(256byte)
} while(1);
```

Thread2 (udp recv thread)
```
create udp socket
do{
	recv(256byte)
} while(1)
```

Just a simple read-write test

**Compile** : make in gcc with c99

**Usage** : ./udp_simple [-c/-p/-b/-h] argument	(-h for help)

			-p		Peer recving data port
			-c		Com port path
			-b		Set baud rate
			-h		For help
## 2. USDG UDP mode packing test
in `usdg_udp_pack`, there are 3 packing condition to test.

(1) packing by size test:
```
<1> send 1 byte data by serial per times
<2> try to recive the data by UDP
<3> check the recive data size is equal to setting or not
<4> also, check the correctness of data
```

(2) packing by time test:
```
<1> send 1K byte data by serial
<2> record the send time
<3> recive 1K byte daya by UDP
<4> record the recive time
<5> evaluate the time interval
<6> check the correctness of data
```
time interval = (recv time - send time) - pack by time setting

show the max interval and average interval

(3) packing by character test
```
<1> send 256 byte data by serial
<2> recive data by UDP
<3> check the correctness of data
<4> check the end of data is equal setting or not
```

**Compile** : make in gcc with c99

**Usage** : ./udp_pack [-p/-b/-i/-c/-e/-s/-h] argument	(-h for help)

			-p		Peer recving data port
			-c		Com port path
			-b		Set baud rate
            -i		time interval (usec)
            -e		End-character
            -s		packing size
			-h		For help
## 3. USDG UDP mode with multi-thread test
in `usdg_udp_multi.c`, there are multiple threads working

Thread 1 (serial write 10K byte thread), write data to udp
```
do{
	write(10K)
}while(1)
```

Thread 2(serial read 5 byte thread), read data from udp
```
do{
	read(5byte)
}while(1)
```

Thread 3~19 (udp send 5 byte thread), send data to serial
```
do{
	create udp socket
    wait for mutex condition signal
    get signal, send 5byte data to serial
    close udp socket
}while(1)
```
every thread will try to preempt the signal, but only one thread can get the signal

This test is a high stress test! 

**Compile** : make in gcc with c99

**Usage** : ./udp_multi [-p/-c/-b/-n/-e/-i/-h] argument	(-h for help)

			-p		Peer recving data port
			-c		Com port path
			-b		Set baud rate
            -n		Number of UDP thread
            -l		EKI data listen port
            -i		EKI IP address
			-h		For help
 
## 4. USDG UDP multicast mode test 
in `usdg_multicast.c`, use signal thread with UDP send 256 byte data and serial read it!

For now, no support data packing test.

**Compile** : make in gcc with c99

**Usage** : ./multicast [-p/-c/-b/-g/-h] argument	(-h for help)

			-p		Data listen port
			-c		Com port path
			-b		Set baud rate
            -g		Multicast group IP
			-h		For help


