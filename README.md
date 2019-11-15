# windows_network_ip_scanner
I couldn't find any decent solution for a lightweight LAN network discovery tool running from a Windows machine, so I decided to create one.\
This tool can discover other devices on your network automaticlly, based on info gathered from all of the installed network cards on your Windows machine. Then, it uses them to send multi-threaded ARP requests and discover which IPs are responding.\
It also prints the machine name if available.
