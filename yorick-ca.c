/*************************************************************************\
 * catools.c
 *
 * Francois Rigaut, 2005July. Some code taken from caget.c and caput.c
 * in the regular catool distribution (see copyright below).
 *
 * Part of the yorick plugin to epics channel access
 * Provides get and put to an epics database
 *
 * Philosophy:
 * at the first caget or caput, the context is created.
 * a list of connected PV is maintained, additionnal one are
 * added as new PV names are accessed.
 * The connection to the database is terminated when an endca() is
 * called or when yorick terminates.
 *
\*************************************************************************/

/*************************************************************************\
 * Copyright (c) 2002 The University of Chicago, as Operator of Argonne
 *     National Laboratory.
 * Copyright (c) 2002 The Regents of the University of California, as
 *     Operator of Los Alamos National Laboratory.
 * Copyright (c) 2002 Berliner Elektronenspeicherringgesellschaft fuer
 *     Synchrotronstrahlung.
 * EPICS BASE Versions 3.13.7
 * and higher are distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cadef.h>
#include <db_access.h>

#include "ydata.h"
#include "pstdlib.h"

#define MAX_CHANNELS     256    // max number of channels managed (sic)
double yorick_ca_pendio_time = 3.0;     // default timeout

int yorick_ca_initialized = 0;
int yorick_ca_debug = 0;

enum
{ debug_none = 0, debug_min, debug_med, debug_max };

char *yorick_ca_errmsg = NULL;

#define CA_LOGMSG(lvl, fmt, ...) { \
   if(yorick_ca_debug>=lvl) { \
      fprintf (stderr,"yorick-ca [%d]: ",lvl); \
      fprintf (stderr,"[%-16s] ", __PRETTY_FUNCTION__); \
      fprintf (stderr, fmt, ## __VA_ARGS__); \
      fflush (stderr); \
   } \
 }

#define ONSORTIE_STATUS(s, fmt, ...)  { \
    if(s != ECA_NORMAL) { \
      if (yorick_ca_errmsg) { free (yorick_ca_errmsg) ; } \
      yorick_ca_errmsg=malloc(256);\
      char tmp_ca_errmsg[256];\
      snprintf (tmp_ca_errmsg, 256, fmt, ## __VA_ARGS__); \
      snprintf(yorick_ca_errmsg,256, "%s - %s", tmp_ca_errmsg, ca_message(s));\
      if(yorick_ca_debug>=debug_min) { \
        fprintf (stderr,"yorick-ca [-]: [%-16s] %s\n", __PRETTY_FUNCTION__, yorick_ca_errmsg); \
        fflush (stderr); \
      } \
    } else { \
      if (yorick_ca_errmsg) { free (yorick_ca_errmsg) ; } \
      yorick_ca_errmsg=NULL; \
    } \
}

typedef struct pv
{
   chid chid;
   evid pevid;
   long dbfType;                // native type
   unsigned long nElems;        // native count of elements
   unsigned long reqElems;      // requested count of elements
   int process_monitor;         // number of monitors raised by ca that have not been handled by yorick
   char *callback_string;       // callback stored as a string yorick will evaluate
   void *value;                 // copy of the last monitor data
//   struct pv *next;           // next pv element to overcome MAX_CHANNELS limit (not implemented)
} pv;

pv *pvs = NULL;                 // all our PVs

extern void (*CleanUpForExit) (void);   // the original yorick exit handler (not in the public API)
static void (*original_cleanup) (void) = NULL;  // a reference to it that will let us interpose our own handler

/*****************************************************************\
 * get_slot_by_name
\*****************************************************************/
int get_slot_by_name(char *pvname)
{
   int i;
   int n = -1;

   CA_LOGMSG(debug_max, "pv: %s\n", pvname);

   // shorten pvname if .VAL was provided
   int n_length = strlen(pvname);
   if (!strcmp(pvname + n_length - 4, ".VAL")) {
      *(pvname + n_length - 4) = '\0';
   }

   for (i = 0; i < MAX_CHANNELS; i++) {
      if (!pvs[i].chid)
         continue;
      if (strcmp(pvname, ca_name(pvs[i].chid)) == 0) {
         n = i;
         CA_LOGMSG(debug_max, "Found %s already registered in channel %d\n", pvname, n);
         break;
      }
   }

   CA_LOGMSG(debug_max, "%s slot %d %s\n", (n == -1) ? "Not Found" : "Found", n, pvname);

   return n;
}

/*****************************************************************\
 * get_free_slot
\*****************************************************************/
int get_free_slot(void)
{
   int i;
   int n = -1;

   CA_LOGMSG(debug_max, "\n");

   for (i = 0; i < MAX_CHANNELS; i++) {
      if (!pvs[i].chid) {
         n = i;
         CA_LOGMSG(debug_max, "Found free slot %d\n", n);
         break;
      }
   }
   if (n == -1) {
      // this one is bad!
      YError("No more pv slots!");
   }

   return n;
}

/*****************************************************************\
 * free_slot
\*****************************************************************/

int free_slot(n)
{
   int status = ECA_NORMAL;

   if (pvs[n].chid) {
      CA_LOGMSG(debug_max, "pv: %s\n", ca_name(pvs[n].chid));

      if (pvs[n].pevid) {
         status = ca_clear_subscription(pvs[n].pevid);
         if (status != ECA_NORMAL) {
            CA_LOGMSG(debug_med, "clear_subscription '%s': %s\n", ca_name(pvs[n].chid), ca_message(status));
            return status;
         } else {
            pvs[n].pevid = NULL;
         }
      }

      status = ca_clear_channel(pvs[n].chid);
      if (status != ECA_NORMAL) {
         CA_LOGMSG(debug_med, "clear_channel '%s': %s\n", ca_name(pvs[n].chid), ca_message(status));
         return status;
      } else {
         pvs[n].chid = NULL;
      }

      if (pvs[n].callback_string) {
         free(pvs[n].callback_string);
         pvs[n].callback_string = NULL;
      }

      if (pvs[n].value) {
         free(pvs[n].value);
         pvs[n].value = NULL;
      }

      pvs[n].process_monitor = 0;
      pvs[n].nElems = 0;
      pvs[n].reqElems = 0;

   }

   return status;
}

/***************************************************************************\
 *
 * Function:  __caget(int n)
 *
 * Description: Issue read requests, wait for incoming data
 *    and print the data according to the selected format
 *
 * Arg(s) int n           -  Number in the pvs array
 *
 * Return(s): Epics error code
 * Push the result in the yorick stack
 *
\***************************************************************************/

int __caget(int n)
{
   void *value = NULL;
   int status = ECA_NORMAL;

   /* Issue CA request */
   if (ca_state(pvs[n].chid) == cs_conn) {

      CA_LOGMSG(debug_max, "pv: %s\n", ca_name(pvs[n].chid));

      Dimension *tmp = tmpDims;
      tmpDims = 0;
      FreeDimension(tmp);

      if (pvs[n].reqElems == 1) {
         tmpDims = (Dimension *) 0;
      } else {
         tmpDims = NewDimension(pvs[n].reqElems, 1L, (Dimension *) 0);
      }

      if (pvs[n].dbfType == DBF_SHORT) {
         Array *a = PushDataBlock(NewArray(&shortStruct, tmpDims));
         value = a->value.s;
      } else if (pvs[n].dbfType == DBF_LONG) {
        // below changed from longStruct to intStruct on Nov 15, 2016.
         Array *a = PushDataBlock(NewArray(&intStruct, tmpDims));
         value = a->value.l;
      } else if (pvs[n].dbfType == DBF_FLOAT) {
         Array *a = PushDataBlock(NewArray(&floatStruct, tmpDims));
         value = a->value.f;
      } else if (pvs[n].dbfType == DBF_DOUBLE) {
         Array *a = PushDataBlock(NewArray(&doubleStruct, tmpDims));
         value = a->value.d;
      } else if (pvs[n].dbfType == DBF_CHAR) {
         Array *a = PushDataBlock(NewArray(&charStruct, tmpDims));
         value = a->value.c;
      } else if (pvs[n].dbfType == DBF_STRING) {
         value = p_malloc(dbr_size_n(DBF_STRING, pvs[n].reqElems));
      } else {
         // YError("Unsupported data type");
         // faky
         status = ECA_BADTYPE;
         CA_LOGMSG(debug_none, "unsupported type %ld for '%s'.\n", pvs[n].dbfType, ca_name(pvs[n].chid));
         return status;
      }

      if (pvs[n].pevid) {
         // todo - camonitor does not have a reqElems
         // so we are sure  reqElems<= nElems
         // otherwise one would test if reqElems > nElems -> fall over a ca_array_get
         char *val = dbr_value_ptr(pvs[n].value, dbf_type_to_DBR_TIME(pvs[n].dbfType));
         memcpy(value, val, pvs[n].reqElems * dbr_value_size[pvs[n].dbfType]);
      } else {
         status = ca_array_get(pvs[n].dbfType, pvs[n].reqElems, pvs[n].chid, value);
         if (status != ECA_NORMAL) {
            // YError("ca_array_get failed.");
            CA_LOGMSG(debug_med, "array_get '%s'.\n", ca_name(pvs[n].chid));
         } else {
            // Wait for completion
            status = ca_pend_io(yorick_ca_pendio_time);
            if (status != ECA_NORMAL) {
               // YError("Read operation timed out: some PV data was not read.");
               CA_LOGMSG(debug_med, "array_get i/o '%s': %s\n", ca_name(pvs[n].chid), ca_message(status));
            }
         }
      }

      if (pvs[n].dbfType == DBF_STRING) {
         int i;
         Array *a = PushDataBlock(NewArray(&stringStruct, tmpDims));
         for (i = 0; i < pvs[n].reqElems; i++) {
            a->value.q[i] = p_strcpy(value + i * MAX_STRING_SIZE);
         }
         p_free(value);
      }

      ONSORTIE_STATUS(status, "caget %s", ca_name(pvs[n].chid));

   } else {
      status = ECA_CHIDNOTFND;
      ONSORTIE_STATUS(status, "caget chid not found");
   }

   return status;
}

/***************************************************************************\
 *
 * Function: __camonitor, __caeventhandler
 *
\***************************************************************************/

void __caeventhandler(struct event_handler_args args)
{
   pv *self = (pv *) ca_puser(args.chid);

   CA_LOGMSG(debug_max, "pv: %s\n", ca_name(args.chid));

   // update our local copy
   memcpy(self->value, args.dbr, dbr_size_n(args.type, args.count));

   // increment count as new data available for callbacks
   if (self->callback_string)
      self->process_monitor++;

}

int __camonitor(int n)
{

   int status;
   // long nelem=1;

   // Issue CA request

   if (ca_state(pvs[n].chid) == cs_conn) {

      CA_LOGMSG(debug_med, "pv: %s\n", ca_name(pvs[n].chid));

      ca_set_puser(pvs[n].chid, (void *) &pvs[n]);      /* self ref for later retreival */
      status = ca_create_subscription(dbf_type_to_DBR_TIME(pvs[n].dbfType), pvs[n].reqElems, pvs[n].chid, DBE_VALUE | DBE_ALARM, __caeventhandler, 0, &pvs[n].pevid);
      // status = ca_create_subscription(dbf_type_to_DBR_TIME(pvs[n].dbfType),nelem, pvs[n].chid, DBE_VALUE | DBE_ALARM, __caeventhandler, 0, &pvs[n].pevid);
      if (status != ECA_NORMAL) {
         // YError("ca_create_subscription failed.");
         CA_LOGMSG(debug_med, "create_subscription '%s': %s\n", ca_name(pvs[n].chid), ca_message(status));
      } else {
         // Wait for completion
         status = ca_pend_io(yorick_ca_pendio_time);
         if (status != ECA_NORMAL) {
            // YError("pend_io ca_create_subscription failed.");
            CA_LOGMSG(debug_med, "create_subscription i/o '%s': %s\n", ca_name(pvs[n].chid), ca_message(status));
         }
      }
      ONSORTIE_STATUS(status, "camonitor %s", ca_name(pvs[n].chid));
   } else {
      status = ECA_CHIDNOTFND;
      ONSORTIE_STATUS(status, "camonitor chid not found");
   }

   return status;
}

/*****************************************************************\
 *
 * function: __caconnect
 *
 * Description: create a channel connection
 *
 * Returns: error code
 *
\*****************************************************************/

int __caconnect(char *pvname, int n)
{

   int status;

   CA_LOGMSG(debug_med, "pv: %s\n", pvname);

   // Issue channel connections
   status = ca_create_channel(pvname, 0, 0, CA_PRIORITY_DEFAULT, &pvs[n].chid);
   if (status != ECA_NORMAL) {
      CA_LOGMSG(debug_med, "create_channel '%s': %s\n", pvname, ca_message(status));
   } else {
      // Wait for channels to connect
      status = ca_pend_io(yorick_ca_pendio_time);
      if (status == ECA_NORMAL) {
         pvs[n].nElems = ca_element_count(pvs[n].chid);
         pvs[n].dbfType = ca_field_type(pvs[n].chid);
         if (pvs[n].dbfType == DBF_ENUM)
            pvs[n].dbfType = DBF_STRING;
      } else {
         CA_LOGMSG(debug_med, "create_channel i/o '%s': %s\n", pvname, ca_message(status));
      }
   }

   ONSORTIE_STATUS(status, "connect %s", pvname);

   return status;
}

/*****************************************************************\
 * __caquit
\*****************************************************************/

void __caquit(void)
{                               /* Shuts down Channel Access */
   int i;
   int retval = 0;

   yorick_ca_initialized = 0;

   for (i = 0; i < MAX_CHANNELS; i++) {
      if (free_slot(i) != ECA_NORMAL)
         retval++;
   }

   if (retval) {
      // too late to care
      // YError ("module did not exit nicely.\n");
   }

   free(pvs);

   ca_context_destroy();

}

/*****************************************************************\
 *
 * function: CleanUpForYorickCA()
 *
 * called when yorick terminates
 * kills the keep-alive thread
 * terminates channel access
 *
\*****************************************************************/
static void CleanUpForYorickCA(void)
{
   __caquit();

   if (original_cleanup)
      original_cleanup();

}

int __cainit(void)
{

   int status;

   // alloc pv structure:
   CA_LOGMSG(debug_max, "Allocating pvs\n");
   pvs = (pv *) calloc(MAX_CHANNELS, sizeof(pv));
   if (pvs == NULL) {
      //YError("Unable to alloc pvs");
      CA_LOGMSG(debug_none, "Unable to alloc pvs\n");
      // faky
      return ECA_ALLOCMEM;
   }
   // create context:
   CA_LOGMSG(debug_max, "Creating context\n");
   status = ca_context_create(ca_disable_preemptive_callback);
   // status  = ca_context_create(ca_enable_preemptive_callback);

   // and check:
   if (status != ECA_NORMAL) {
      CA_LOGMSG(debug_med, "ca_context_create: %s\n", ca_message(status));
   } else {
      // we're connected:
      yorick_ca_initialized = 1;

      // set up things for clean up at quit
      original_cleanup = CleanUpForExit;
      CleanUpForExit = &CleanUpForYorickCA;
   }

   ONSORTIE_STATUS(status, "init");

   return status;
}

/*****************************************************************\
 * Y__manage_monitors
\*****************************************************************/

void Y__manage_monitors(void)
{
   int i;
   int j = 0;
   int nmon = 0;
   char *msg;

   // this function is periodically called from Yorick
   // first we poll channel access to take care of the
   // background activity, then we check for monitors
   // that might need to be processed (build a long string
   // that has to be eval (funcdef) by yorick

   ca_poll();

   for (i = 0; i < MAX_CHANNELS; i++) {
      if (pvs[i].process_monitor)
         nmon++;
   }

   if (nmon == 0) {
      PushDataBlock(RefNC(&nilDB));
      return;
   }

   Dimension *tmp = tmpDims;
   tmpDims = 0;
   FreeDimension(tmp);
   tmpDims = NewDimension(nmon, 1L, (Dimension *) 0);

   Array *a = PushDataBlock(NewArray(&stringStruct, tmpDims));
   for (i = 0; i < MAX_CHANNELS; i++) {
      if (pvs[i].process_monitor) {
         msg = p_strncat(pvs[i].callback_string, " \"", 0L);
         msg = p_strncat(msg, ca_name(pvs[i].chid), 0L);
         a->value.q[j] = p_strncat(msg, "\"", 0L);
         pvs[i].process_monitor = 0;
         j++;
      }
   }
}

/*****************************************************************\
 * Y_ca_is_monitored
\*****************************************************************/

void Y_ca_is_monitored(int nArgs)
{
   char *pvname = YGetString(sp - nArgs + 1);
   int n;

   CA_LOGMSG(debug_med, "pvname %s\n", pvname);

   n = get_slot_by_name(pvname);

   if (n == -1) {
      // YError("Channel is not managed");
      PushIntValue(0);
   } else {
      PushIntValue(pvs[n].pevid != 0);
   }
}

/*****************************************************************\
 * Y_ca_clear_monitor
\*****************************************************************/

void Y_ca_clear_monitor(int nArgs)
{
   char *pvname = YGetString(sp - nArgs + 1);
   int n;
   int status;

   CA_LOGMSG(debug_med, "pvname %s\n", pvname);

   n = get_slot_by_name(pvname);

   if (n == -1) {
      // this is minor so don't raise an exception
      // YError("Channel is not managed");
      PushIntValue(1);
      return;
   }

   if (!pvs[n].pevid) {
      // this is minor so don't raise an exception
      // YError("Channel is not monitored");
      PushIntValue(1);
      return;
   }

   status = ca_clear_subscription(pvs[n].pevid);

   if (status != ECA_NORMAL) {
      CA_LOGMSG(debug_med, "clear_subscription '%s': %s\n", pvname, ca_message(status));
      // YError("Failed to ca_clear_subscription");
      PushIntValue(1);
      return;
   }

   pvs[n].pevid = 0;
   pvs[n].process_monitor = 0;
   if (pvs[n].callback_string) {
      free(pvs[n].callback_string);
      pvs[n].callback_string = NULL;
   }

   PushIntValue(0);

}

/*****************************************************************\
 * Y_ca_clear_all_monitors
\*****************************************************************/

void Y_ca_clear_all_monitors(int nArgs)
{
   int n, retval = 0;
   int status;

   for (n = 0; n < MAX_CHANNELS; n++) {
      if (!pvs[n].pevid)
         continue;
      status = ca_clear_subscription(pvs[n].pevid);
      if (status == ECA_NORMAL) {
         pvs[n].pevid = 0;
         pvs[n].process_monitor = 0;
         if (pvs[n].callback_string) {
            free(pvs[n].callback_string);
            pvs[n].callback_string = NULL;
         }
      } else {
         retval++;
      }
   }

   PushIntValue(retval);

}

/*****************************************************************\
 * Y__caget
 *
 * Description: Main entry point for yorick function caget
 *
 * Input arguments:
 *  pvname:   PV channel name
 *  type:     type to return
 *  nelem:    number of elements to return
 *
 * Returns: void. The data are push into the yorick stack in __caget()
 *
\*****************************************************************/

void Y__caget(int nArgs)
{
   /* caget(pvname,type,nelem=) */
   char *pvname = YGetString(sp - nArgs + 1);
   int nelem = YGetInteger(sp - nArgs + 2);
   int keepalive = YGetInteger(sp - nArgs + 3);
   int status;

   CA_LOGMSG(debug_med, "pvname %s\n", pvname);

   int n = -1;

   // if we have to init the channel access:
   if (!yorick_ca_initialized) {
      status = __cainit();
      if (status != ECA_NORMAL) {
         // YError("Problem Initializing CA connection");
         PushDataBlock(RefNC(&nilDB));
         return;
      }
      CA_LOGMSG(debug_max, "Connection Initialized\n");
   }

   if (keepalive) {
      // try to get the channel if already managed
      n = get_slot_by_name(pvname);
   }
   // if not, get a free channel
   if (n == -1) {

      n = get_free_slot();

      // Connect channels
      status = __caconnect(pvname, n);

      if (status != ECA_NORMAL) {
         // we're already in error, so ignore the one coming from cleanup
         free_slot(n);
         // YError("Could not connect to channel");
         PushDataBlock(RefNC(&nilDB));
         return;
      }
   }
   // Processing the other requests (nelem and type):
   if (nelem) {
      if (nelem > pvs[n].nElems) {
         if (!keepalive) {
            // we're already in error, so ignore the one coming from cleanup
            free_slot(n);
         }
         // YError("Requesting too many elements");
         // faky
         status = ECA_BADTYPE;
         PushDataBlock(RefNC(&nilDB));
         return;
      }
      pvs[n].reqElems = nelem;
   } else {
      pvs[n].reqElems = pvs[n].nElems;
   }

   // do the caget
   status = __caget(n);

   if (!keepalive) {
      free_slot(n);
   }

   if (status != ECA_NORMAL) {
      PushDataBlock(RefNC(&nilDB));
      return;
   }

   PopTo(sp - nArgs - 1);
   Drop(nArgs);

}

/*****************************************************************\
 * Y__camonitor
 *
 * Description: Builtin function _camonitor
 *
 * Input arguments:
 *  pvname:   PV channel name
 *  funcname: callback function
 *
 * Returns: void. events are triggered and managed by __caeventhandler()
 *
\*****************************************************************/

void Y__camonitor(int nArgs)
{
   /* camonitor(pvname,callback) */
   char *pvname = YGetString(sp - nArgs + 1);
   char *funcname = YGetString(sp - nArgs + 2);

   CA_LOGMSG(debug_med, "pvname %s\n", pvname);
   CA_LOGMSG(debug_med, "funcname %s\n", funcname);

   int n = -1;
   int status;
   int funcname_len = strlen(funcname);
   // if we have to init the channel access:
   if (!yorick_ca_initialized) {
      status = __cainit();
      if (status != ECA_NORMAL) {
         //YError("Problem Initializing CA connection");
         PushIntValue(1);
         return;
      }
      CA_LOGMSG(debug_max, "Connection Initialized\n");
   }
   // try to get the channel if already managed
   n = get_slot_by_name(pvname);

   // if not, get a free channel
   if (n == -1) {

      n = get_free_slot();

      // Connect channels

      status = __caconnect(pvname, n);

      if (status != ECA_NORMAL) {
         // we're already in error, so ignore the one coming from cleanup
         free_slot(n);
         // YError("Could not connect to channel");
         PushIntValue(1);
         return;
      }

   }

   if (pvs[n].pevid) {
      if (funcname_len) {
         // cannot register the callback, this could be bad
         // YError("Channel already monitored, callback ignored");
         PushIntValue(1);
      } else {
         // this is minor, don't raise an exception
         PushIntValue(1);
      }
      return;
   }

   if (funcname_len) {
      pvs[n].callback_string = malloc(funcname_len + 1);
      strcpy(pvs[n].callback_string, funcname);
   } else {
      pvs[n].callback_string = NULL;
   }

   // todo: add an option to reqElems on camonitor
   pvs[n].reqElems = pvs[n].nElems;

   /*
    * allocate mem for the monitored value
    */

   pvs[n].value = calloc(1, dbr_size_n(dbf_type_to_DBR_TIME(pvs[n].dbfType), pvs[n].nElems));
   if (pvs[n].value == NULL) {
      //YError("Failed to alloc memory");
      PushIntValue(1);
      return;
   }
   // initiate monitor subsciption:
   __camonitor(n);

   PushIntValue(0);

}

/*****************************************************************\
 * Y__caput
 *
 * Description: Main entry point for yorick function caput
 *
 * Input arguments:
 *  pvname:    PV channel name
 *  nelem:     number of elements to return
 *  datatype:  type used to put the data
 *  keepalive: register this channel or use transient
 *  nelem:     number of elements to return
 *  data:      data pointer
 *
 * Returns: void. YError is called if there is a problem.
 *
\*****************************************************************/

void Y__caput(int nArgs)
{
   /* _caput,pvname,nelem,type,data */
   char *pvname = YGetString(sp - nArgs + 1);
   int nelem = YGetInteger(sp - nArgs + 2);
   int datatype = YGetInteger(sp - nArgs + 3);
   int keepalive = YGetInteger(sp - nArgs + 4);

   CA_LOGMSG(debug_med, "pvname %s\n", pvname);

   int n = -1;
   int status;

   // if we have to init the channel access:
   if (!yorick_ca_initialized) {
      status = __cainit();
      if (status != ECA_NORMAL) {
         // YError("Problem Initializing CA connection");
         PushIntValue(1);
         return;
      }
   }

   if (keepalive) {
      // try to get the channel if already managed
      n = get_slot_by_name(pvname);
   }
   // if not, get a free channel
   if (n == -1) {

      n = get_free_slot();

      // Connect channels
      status = __caconnect(pvname, n);

      if (status != ECA_NORMAL) {
         // we're already in error, so ignore the one coming from cleanup
         free_slot(n);
         // YError("Could not connect to channel");
         PushIntValue(1);
         return;
      }

   }

   if (nelem > pvs[n].nElems) {
      if (!keepalive) {
         // we're already in error, so ignore the one coming from cleanup
         free_slot(n);
      }
      // YError("Trying to put too many elements");
      PushIntValue(1);
      return;
   }

   if (datatype == DBF_STRING) {
      status = ca_array_put(datatype, nelem, pvs[n].chid, yarg_sq(0));
   } else {
      status = ca_array_put(datatype, nelem, pvs[n].chid, yarg_sp(0));
   }

   if (status != ECA_NORMAL) {
      CA_LOGMSG(debug_med, "put '%s': %s\n", pvname, ca_message(status));
   } else {
      status = ca_pend_io(yorick_ca_pendio_time);
      if (status != ECA_NORMAL) {
         CA_LOGMSG(debug_med, "put i/o '%s': %s\n", pvname, ca_message(status));
      }
   }

   if (!keepalive) {
      free_slot(n);
   }

   ONSORTIE_STATUS(status, "put %s", pvname);
   PushIntValue((status != ECA_NORMAL));
}

/*****************************************************************\
 * Y_ca_quit
\*****************************************************************/

void Y_ca_quit(int nArgs)
{
   __caquit();
}

/*****************************************************************\
 * Y_ca_report
\*****************************************************************/

void Y_ca_report(int nArgs)
{
   int i;

   fprintf(stderr, "ca_initialized  = %d\n", yorick_ca_initialized);
   fprintf(stderr, "ca_pendio_time  = %f\n", yorick_ca_pendio_time);
   for (i = 0; i < MAX_CHANNELS; i++) {
      if (pvs[i].chid) {
         fprintf(stderr, "%3d name: %s ", i, ca_name(pvs[i].chid));
         fprintf(stderr, "type: %d ", ca_field_type(pvs[i].chid));
         fprintf(stderr, "dim: %ld ", ca_element_count(pvs[i].chid));
         if (pvs[i].pevid) {
            fprintf(stderr, "[monitored] ");
            fprintf(stderr, "pmon: %d ", pvs[i].process_monitor);
            if (pvs[i].callback_string) {
               fprintf(stderr, "callback: %s ", pvs[i].callback_string);
            }
         }
         // pv2str(pvs[i]
         fprintf(stderr, "\n");
      }
   }
}

/*****************************************************************\
 * Y_ca_release
\*****************************************************************/

void Y_ca_release(int nArgs)
{
   int status;

   char *pvname = YGetString(sp - nArgs + 1);
   int n;

   CA_LOGMSG(debug_med, "pvname %s\n", pvname);

   n = get_slot_by_name(pvname);

   if (n == -1) {
      // this is minor, don't raise an exception
      // YError("Channel is not managed");
      PushIntValue(1);
      return;
   }

   status = free_slot(n);

   ONSORTIE_STATUS(status, "release %s", pvname);
   PushIntValue(status != ECA_NORMAL);
}

/*****************************************************************\
 * Y_ca_get_errmsg
\*****************************************************************/

void Y_ca_get_errmsg(int nArgs)
{
   Array *a = PushDataBlock(NewArray(&stringStruct, tmpDims));
   a->value.q[0] = p_strcpy(yorick_ca_errmsg);
}
