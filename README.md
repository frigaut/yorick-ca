# Yorick EPICS Channel Access plugin

This plugin provide access to epics channel access functions. It requires the EPICS 3.14 libraries (https://epics.anl.gov/index.php).

## Available functions
For more info: help,function in yorick.

```
caget...................get a pv value
caput...................put a pv value
camonitor...............monitor with callback
ca_clear_monitor........clear a monitor
ca_clear_all_monitors...clear all monitors
ca_is_monitored.........check for a monitored variable
ca_release..............release a channel from the list of managed pv
ca_report...............print misc ca info on stderr
ca_timeout..............global (double), get/set pend i/o default timeout
ca_debug................(global variable) output to stderr 0: silent, 1:err, 2: +debug, 3: +trace
ca_quit.................was endca
ca_set_timeout..........helper function for ca_timeout
ca_set_debug............helper function for ca_debug
ca_get_errmsg...........get the last descriptive error message
```

## Environment setting

As for regular CA use, this requires the use of a number of environment variables.
In my case, I have:
```
EPICS_CA_ADDR_LIST=172.17.106.111 172.16.2.38 172.17.3.255 172.17.2.255 172.17.105.20 172.17.65.255
EPICS_HOST_ARCH=linux-x86_64
EPICS=/home/frigaut/packages/epics
EPICS_CA_MAX_ARRAY_BYTES=4200000
EPICS_CA_AUTO_ADDR_LIST=NO
PATH=/home/frigaut/mcao-myst/bin:/home/frigaut/packages/epics/base/bin/linux-x86_64/:/home/frigaut/bin:/home/frigaut/packages/yorick-linux/yorick/relocate/bin:/usr/local/bin:/usr/local/sbin:/usr/bin:/usr/bin/site_perl:/usr/bin/vendor_perl:/usr/bin/core_perl
EPICS_BASE=/home/frigaut/packages/epics/base
LD_LIBRARY_PATH=/home/frigaut/packages/epics/base/lib/linux-x86_64
```
The `LD_LIBRARY_PATH` is only necessary because I have EPICS installed on a non-standard location.
