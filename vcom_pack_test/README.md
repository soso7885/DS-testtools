The VCOM packing conditions test tool
==========================================

The program
------------
**tool**

The program has three 4 test mode :

* For normal test : send 256 byte data then recive the data.

* For pack by size test : send 1 byte continous till the pack size and recive it.

* For pack by interval test : send 256 byte then recive the data, and calculate the interval time.

* For pack by character test : send 256 byte then recve the data, and detect the end character.

Usage
------

* Compile : make in gcc

* Excute : ./tool [com port] [baudrate]

* All the options : 
<pre>
@ end-character setting in heximal
@ interval time setting in ms
@ data packing size setting in byte
</pre>

**0 for disable the options**
