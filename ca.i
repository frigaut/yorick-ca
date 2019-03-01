CA_VERSION = 1.0;

local ca;
/* DOCUMENT ca plugin: EPICS channel access

   Available functions (for more info, help,function)
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
    
 */

plug_in, "ca";

_capoll_started = 0;
_capoll_sleep = 0.1;

if (keepalive_default==[]) keepalive_default=0n;

func caget(caname, nelem=, keepalive=)
/* DOCUMENT func caget(caname [, nelem=, keepalive=])
   Epics channel access. Wrapper for the ca library function caget.
   Fetch and return the value of pvname (channel) variable "caname"

   - nelem is an optional number of element to retrieve. if ommited,
     caget will get this value from the epics database.

   - keepalive is an optional flag to re-use ca connection circuitry
     (default=keepalive_default, 0 means off).

   EXAMPLE: d = caget("mc:elCurrentPos")
   SEE ALSO: camonitor, caput
*/
{
  extern _capoll_started;

  if (nelem==[]) nelem=0n;
  else nelem = int(nelem);

  if (keepalive==[]) keepalive=int(keepalive_default);
  else keepalive = int(keepalive);

  status = _caget(caname, nelem, keepalive);

  if (status==[]) after_epics_error;
  
  if (!_capoll_started) {
    _capoll_started = 1;
    capoll;
  }

  return status;
}



/*****************************************************************/

func camonitor(caname, callback, ellapse=, ellapsefunc=)
/* DOCUMENT camonitor(caname, callback, ellapse=, ellapsefunc=)
   Sets up a monitor on a process variable, with a callback function.

   Parameters:
   caname....... process variable (string)
   callback..... function to call when caname changes (string)

   Keywords:
   ellapse...... after "ellapse" seconds, "ellapsefunc" is called (this
                 can be use to, e.g., stop the monitor)
   ellapsefunc.. function called after ellapse seconds
   
   Note that callback must be quoted ("") while ellapsefunc is not.
             
   Examples:

   func callback(a) { write,format="%s = %s\n> ",a,caget(a,string); }
   camonitor,"mc:elCurrentPos","callback1"

   ->  will monitor "mc:elCurrentPos". Each time the variable changes, the
       user-defined function "callback1" will be called.

   func callback(a) { write,format="%s = %s\n> ",a,caget(a,string); }
   func on_ellapse  { ca_clear_monitor,"mc:elCurrentPos"; }
   camonitor,"mc:elCurrentPos","callback",ellapse=10,ellapsefunc=on_ellapse;

   -> will monitor "mc:elCurrentPos", using "callback1" as a call back
      function, and will call "ellapsefunc"
   
   SEE ALSO: caget, caput
 */
{
  extern _capoll_started;

// default callback print as string is more involved .. leave it out for now
//  if (callback==[]) callback="_prt_callback";
  if (callback==[]) callback="";

  status = _camonitor(caname, callback);
  
  if (status) {
    after_epics_error;
    //exit;
  }

  if (!_capoll_started) {
    _capoll_started = 1;
    capoll;
  }

  if (is_func(ellapsefunc)) {
    if (is_void(ellapse)) ellapse=60;
    after,ellapse,ellapsefunc;
  }
  
  return status;
}

func _prt_callback(pv) { write,format="%s = %s\n> ",pv,caget(pv,string); }

/*****************************************************************/

func caput(caname,data, keepalive=)
/* DOCUMENT func caput(caname, data [,keepalive=])
   Epics channel access.
   Write the data to the specified pvname (channel) 
   The dimensions of "data" have to be compatible with its dimension in
   the epics database.

   - keepalive is an optional flag to re-use ca connection circuitry
     (default=keepalive_default).

  SEE ALSO: caget, camonitor
*/
{
  extern _capoll_started;

  nelem    = int(numberof(data));
  datatype = structof(data);

  if      (datatype==string) { type = 0n; }
  else if (datatype==short)  { type = 1n; }
  else if (datatype==float)  { type = 2n; }
  else if (datatype==char)   { type = 4n; }
  else if (datatype==long)   { type = 5n; }
  else if (datatype==double)  { type = 6n; }
  else if (datatype==int) {
    if (sizeof(1n)==sizeof(1s)) type = 1n;
    if (sizeof(1n)==sizeof(1l)) type = 5n; 
  } else { 
    error,"Unsupported type. Valid types "+
      "are char, short, int, long, float, double or string ";
    //ca_errmsg="Unsupported type. Valid types "+
    //  "are char, short, int, long, float, double or string ";
    return 1;
  }
  
  if (keepalive==[]) keepalive=int(keepalive_default);
  else keepalive = int(keepalive);

  if (datatype==string) {
    status = _caput(caname, nelem, type, keepalive, data);
  } else {
    status = _caput(caname, nelem, type, keepalive, &data);
  }
  
  if (status) {
    after_epics_error;
    //exit;
  }

  if (!_capoll_started) {
    _capoll_started = 1;
    capoll;
  }
  
  return status;
}

/*****************************************************************/
/* helper functions
/*****************************************************************/      

func ca_set_timeout(t) {
   extern ca_timeout;
   ca_timeout=t;
   return ca_timeout;
}
func ca_set_debug(l) {
   extern ca_debug;
   ca_debug=l;
   return ca_debug;
}
 
/*****************************************************************/

func capoll {
  callbacks = _manage_monitors();
  for (i=1;i<=numberof(callbacks);i++) funcdef(callbacks(i));
  after,_capoll_sleep,capoll;
}

func generic_after_epics_error(void)
{
  msg = ca_get_errmsg();
  if (msg==[]) msg="Unknown";
  error,swrite(format="Epics error: %s",msg(1));
}
if (after_epics_error==[]) after_epics_error=generic_after_epics_error;

// private API

extern _caget;
extern _caput;
extern _camonitor;
extern _manage_monitors;
extern ca_get_errmsg;

// public variables

extern ca_debug;
 /* EXTERNAL yorick_ca_debug */
reshape, ca_debug, int;

extern ca_timeout;
 /* EXTERNAL yorick_ca_pendio_time */
reshape, ca_timeout, double;
 
// public API

extern ca_report;
extern ca_quit;

extern ca_clear_monitor;
/* DOCUMENT ca_clear_monitor(caname)
   End monitor on process variable caname
   SEE ALSO: ca_clear_all_monitors, ca_is_monitored
 */
extern ca_clear_all_monitors;
/* DOCUMENT ca_clear_all_monitors
   End monitor on all process variables
   SEE ALSO: ca_clear_monitor, ca_is_monitored
 */
extern ca_is_monitored;
/* DOCUMENT ca_is_monitored(caname)
   Returns 1 if process variable caname is currently being monitored by
   this yorick process, 0 otherwise.     
   SEE ALSO:
 */
extern ca_release;
/* DOCUMENT ca_release(caname)
   Release a process variable from the list of managed pv.     
   SEE ALSO:
 */

