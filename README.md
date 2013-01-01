lsupnp
======

This is a command-line program that discovers all Universal Plug and Play (UPnP) devices on a network and lists them (lsupnp = list upnp). It does this by sending out an HTTP device discovery request to the SSDP multicast address and then listening for responses from UPnP devices.

For example, running this program on my home network reveals three UPnP devices: a NAS server, a Roku streaming player, and the router.

How to build
------------
Assuming gcc and make are installed, building the program is as easy as running `make` from the source directory.

Usage
-----
Type `./lsupnp` at the command line. You may find the following command-line options useful:

-p [port]: Bind the source UDP port to the specified port number. This option is useful if you are running a firewall, because devices send their responses to the same UDP port that sent the discovery request. Therefore you can add a firewall rule to allow device responses through on the specified port.

-r: Do a reverse DNS lookup on the IP address of any responding device.

-v: Display verbose information, such as raw HTTP headers and responses.

