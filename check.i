require,"ca.i";


pv = "rtc:heartBeat.VALA";

write,"Check caget:";
write,format="%s=%d\n",pv,caget(pv);

write,"Check camonitor:";
func print_monitor(a) {
  write,format="\n%s = ",a;
  write,caget(a);
  write,format="%s","> ";
}


var1=0;

func var1callback(var) {
  extern var1;
  var1=caget(var);
  if (var1=="IDLE") {
    write,"var1=IDLE!";
    ca_clear_monitor,var;
  }
}

func check_after_epics_error(void)
{
  write,format="Got epics error: %s\n",ca_get_errmsg();
  write,"Good, that was expected\n";
}

func var1ellapse {
  extern pv;
  write,format="\ncheck if %s is monitored: %d\n",pv,ca_is_monitored(pv);
  write,format="\n%s\n","End monitoring";
  ca_clear_monitor,pv;
  write,format="%s\n> ","Type \"next\" to proceed with test";
}  
  
func next {
  write,format="%s\n","Running for 3s";
  camonitor,pv,"print_monitor",ellapse=3,ellapsefunc=next2;
}

func next2 {
  extern pv;
  extern after_epics_error;
  extern ca_debug, ca_timeout;
  
  write,"Set verbose to \"full\"";
  ca_debug=10;
  write,"Doing a caget..";
  caget,pv;
  write,"Set verbose to \"none\"";
  ca_debug=0;
  write,"Print some stats..";
  ca_report;
  write,"Change the i/o timeout to 0.1";
  ca_timeout=0.1;
  write,"Overriding default handler for next two test";
  old_handler = after_epics_error
  after_epics_error=check_after_epics_error
  write,"Doing a caget on unexisting channel";
  status = caget("xxx:ooo");
  if (status == []) {
     write,"Good, I got a bad caget";
  } else {
      write,"Bad, caget on dummy channel returned Ok";
  }
  write,"Change the i/o timeout to 5.0";
  ca_timeout=5.0;
  write,"Doing a caget on unexisting channel";
  status = caget("xxx:ooo");
  if (status == []) {
     write,"Good, I got a bad caget";
  } else {
      write,"Bad, caget on dummy channel returned Ok";
  }
  write,"Restoring default handler";
  caget,"xxx:ooo";
  after_epics_error=old_handler
  write,"Do a keepalive get";
  caget,pv,keepalive=1;
  write,"Print some stats..";
  ca_report;
  write,"Release this channel";
  ca_release,pv;
  write,"Print some stats..";
  ca_report;
  write,"Done";
  quit;
  
}

func stop {
  write,"Doing \"ca_clear_monitor,pv\"";
  ca_clear_monitor,pv;
  write,"Check finished";
  quit;
}

func stop {
  write,"Doing \"ca_clear_monitor,pv\"";
  ca_clear_monitor,pv;
  write,"Check finished";
  quit;
}

camonitor,pv,"var1callback",ellapse=3,ellapsefunc=var1ellapse;
write,format="Monitoring %s in variable \"var1\" for 3s\n",pv;
write,format="%s\n","Type \"var1\" @ prompt to print out monitored var.";

