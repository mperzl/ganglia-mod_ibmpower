/******************************************************************************
 *
 *  This module implements IBM POWER5/6/7/8-specific extensions like:
 *    - SPLPAR
 *    - SMT
 *    - CPU Entitlement
 *    - Capped/Uncapped
 *    - etc.
 *
 *  The code has been tested with on the following systems (all PowerPC-based):
 *    - SLES 9, SLES 10, SLES 11 and SLES 12
 *    - RHEL 4, RHEL 5, RHEL 6 and RHEL 7
 *    - openSUSE 13.1-42.3
 *    - Fedora Core 16-26
 *    - Debian 7-9
 *    - Ubuntu 14.04-17.10
 *
 *  Written by Michael Perzl (michael@perzl.org)
 *
 *  Version 0.7, Oct 26, 2017
 *
 *  As long as I have not figured out how to obtain the number of cores
 *  contained in the global shared processor pool this will not be called
 *  version 1.x.
 *
 *  Version 0.7:  Oct 26, 2017
 *                - added KVM Guest detection
 *                  (--> lots of changes )
 *                - added CPU type detection
 *                  (--> cpu_type_func() )
 *                - fixed SMT detection
 *                  (--> smt_func() )
 *                - fixed Firmware detection
 *                  (--> fwversion_func() )
 *
 *  Version 0.6:  Oct 21, 2013
 *                - changed value of BUFFSIZE for 'my_timely_file' structure
 *                - rewrote the cpu_ec_func() to not call 'cpu_used_func()'
 *                  directly anymore but to remember the last "cpu_used" value.
 *
 *  Version 0.5:  Jun 10, 2013
 *                - fixed the 'my_timely_file' structure
 *                - added new metric cpu_ec
 *                  (--> cpu_ec_func() )
 *                - improved SMT detection
 *                  (--> smt_func() )
 *
 *  Version 0.4:  Feb 09, 2012
 *                - added new metric cpu_pool_id
 *                  (--> cpu_pool_id_func() )
 *
 *  Version 0.3:  Apr 27, 2010
 *                - added sanity check for cpu_pool_idle_func()
 *                - added new metric fwversion
 *                  (--> fwversion_func() )
 *                - fixed cpu_used_func() for systems which have
 *                  /proc/ppc64/lparcfg and the purr stanza does exist but
 *                  returns garbage because the CPU does not have a PURR
 *                  register, e.g., true for PowerPC970 (--> JS20, JS21)
 *
 *  Version 0.2:  Feb 10, 2010
 *                - improved cpu_used() function
 *                - added IO ops/sec metric
 *                  (--> disk_iops_func() )
 *                - changed metric type from GANGLIA_VALUE_FLOAT to
 *                  GANGLIA_VALUE_DOUBLE and changed unit to bytes/sec
 *                  for disk_read_func() and disk_write_func()
 *                - added model_name metric
 *                  (--> model_name_func() )
 *
 *  Version 0.1:  Dec 11, 2008
 *                - initial release
 *
 ******************************************************************************/

/*
 * The ganglia metric "C" interface, required for building DSO modules.
 */

#include <gm_metric.h>


#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "gm_file.h"
#include "libmetrics.h"


#ifndef BUFFSIZE
#define BUFFSIZE 131072
#endif


typedef struct
{
   uint32_t last_read;
   uint32_t thresh;
   char *name;
   char *buffer[BUFFSIZE];
} my_timely_file;


static my_timely_file proc_cpuinfo = { 0, 1, "/proc/cpuinfo", {0} };
static my_timely_file proc_diskstats = { 0, 1, "/proc/diskstats", {0} };
static my_timely_file proc_stat = { 0, 1, "/proc/stat", {0} };
static my_timely_file proc_ppc64_lparcfg = { 0, 1, "/proc/ppc64/lparcfg", {0} };

static time_t boottime = 0;

static int purrUsable = FALSE;

static float last_cpu_used = 0.0;

static int LPARcfgExists = FALSE;   /* /proc/ppc64/lparcfg exists? */

static int KVM_Guest = FALSE;  /* Running as KVM guest? */

static int SPLPAR_Mode = FALSE;  /* Running as SPLPAR? */

static int KVM_Mode = 0;  /* 1 = KVM guest
                             2 = LE host  (e.g., Ubuntu bare-metal, IBM PowerKVM etc.)
                             0 = otherwise */



static int
my_fileexists( char *fName )
{
   FILE    *f;


   f = fopen( fName, "r" );

   if (f)
   {
      fclose( f );
      return( TRUE );
   }
   else
      return( FALSE );
}



static char *
my_update_file( my_timely_file *tf )
{
   int now, rval;


   now = time( NULL );
   if (now - tf->last_read > tf->thresh)
   {
      rval = slurpfile( tf->name, tf->buffer, BUFFSIZE );
      if (rval == SYNAPSE_FAILURE)
      {
         err_msg( "my_update_file() got an error from slurpfile() reading %s",
	          tf->name );
         return( (char *) NULL );
      }
      else
         tf->last_read = now;
   }

   return( tf->buffer[0] );
}



static time_t
boottime_func_CALLED_ONCE( void )
{
   char   *p;
   time_t  boottime;


   p = my_update_file( &proc_stat ); 

   p = strstr( p, "btime" );
   if (p)
   { 
      p = skip_token( p );
      boottime = strtod( p, (char **) NULL );
   }
   else
      boottime = 0;

   return( boottime );
}



g_val_t
model_name_func( void );

static void
CheckPURRusability( void )
{
   g_val_t val;


   purrUsable = TRUE;

   val = model_name_func();

   if ((! strncmp( val.str, "IBM,8842-21X", 12 )) ||
       (! strncmp( val.str, "IBM,8842-41X", 12 )) ||
       (! strncmp( val.str, "IBM,8844-31",  11 )) ||
       (! strncmp( val.str, "IBM,8844-41",  11 )) ||
       (! strncmp( val.str, "IBM,8844-51",  11 )))
      purrUsable = FALSE;
}



g_val_t
capped_func( void )
{
   g_val_t val;
   char *p;
   int i;


   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "capped=" );
   else
      p = (char *) NULL;

   if (p)
      i = strtol( p+7, (char **) NULL, 10 );
   else
      i = -1;

   strcpy( val.str, i == -1 ? "No SPLPAR-capable system" : (i == 1 ? "yes" : "no" ) ); 
   return( val );
}



g_val_t
cpu_entitlement_func( void )
{
   g_val_t  val;
   char    *p;
   int      cpus;


   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "partition_entitled_capacity=" );
   else
      p = (char *) NULL;

   if (p)
      val.f = (float) strtol( p+28, (char **) NULL, 10 ) / 100.0;
   else
   {
/* find out the number of CPUs in the system/LPAR */

      p = my_update_file( &proc_stat );

/* Skip initial "cpu" token and find first real cpu "cpu0" */
      p = strstr( p+3, "cpu");

      cpus = 1;
      while ((p = strstr( p+3, "cpu" )))
         cpus++;

      val.f = cpus;
   }

   return( val );
}



g_val_t
cpu_in_lpar_func( void )
{
   g_val_t  val;
   char    *p;
   int      cpus;


   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "partition_active_processors=" );
   else
      p = (char *) NULL;

   if (p)
      val.int32 = strtol( p+28, (char **) NULL, 10 );
   else
   {
/* find out the number of CPUs in the system/LPAR */

      p = my_update_file( &proc_stat );

/* Skip initial "cpu" token and find first real cpu "cpu0" */
      p = strstr( p+3, "cpu");

      cpus = 1;
      while ((p = strstr( p+3, "cpu" )))
         cpus++;

      val.int32 = cpus;
   }

   return( val );
}



g_val_t
cpu_in_machine_func( void )
{
   g_val_t  val;
   char    *p;
   int      cpus;


   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "system_potential_processors=" );
   else
      p = (char *) NULL;

   if (p)
      val.int32 = strtol( p+28, (char **) NULL, 10 );
   else
   {
/* find out the number of CPUs in the system/LPAR */

      p = my_update_file( &proc_stat );

/* Skip initial "cpu" token and find first real cpu "cpu0" */
      p = strstr( p+3, "cpu");

      cpus = 1;
      while ((p = strstr( p+3, "cpu" )))
         cpus++;

      val.int32 = cpus;
   }

   return( val );
}



g_val_t
cpu_in_pool_func( void )
{
   g_val_t  val;
   char    *p;
   int      cpus;


   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "pool_num_procs=" );
   else
      p = (char *) NULL;

   if (p)
      val.int32 = strtol( p+15, (char **) NULL, 10 );
   else
   {
/* find out the number of CPUs in the system/LPAR */

      p = my_update_file( &proc_stat );

/* Skip initial "cpu" token and find first real cpu "cpu0" */
      p = strstr( p+3, "cpu");

      cpus = 1;
      while ((p = strstr( p+3, "cpu" )))
         cpus++;

      val.int32 = cpus;
   }

   return( val );
}



g_val_t
cpu_in_syspool_func( void )
{
   g_val_t  val;
   char    *p;
   int      cpus;


/* this is still not implemented for multiple shared processor pools */
   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "pool_num_procs=" );
   else
      p = (char *) NULL;

   if (p)
      val.int32 = strtol( p+15, (char **) NULL, 10 );
   else
   {
/* find out the number of CPUs in the system/LPAR */

      p = my_update_file( &proc_stat );

/* Skip initial "cpu" token and find first real cpu "cpu0" */
      p = strstr( p+3, "cpu");

      cpus = 1;
      while ((p = strstr( p+3, "cpu" )))
         cpus++;

      val.int32 = cpus;
   }

   return( val );
}



g_val_t
cpu_pool_id_func( void )
{
   g_val_t val;
   int pool_id;
   char *p;


   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "pool=" );
   else
      p = (char *) NULL;

   if (p)
      pool_id = strtol( p+5, (char **) NULL, 10 );
   else
      pool_id = -1;

   val.int32 = pool_id;

   return( val );
}



#define MAX_CPU_POOL_IDLE (256.0)

g_val_t
cpu_pool_idle_func( void )
{
   g_val_t val;
   static long long pool_idle_saved = 0LL;
   long long pool_idle, pool_idle_diff, timebase;
   static double last_time = 0.0;
   static float last_val  = 0.0;
   double now, delta_t;
   struct timeval timeValue;
   struct timezone timeZone;
   char *p;


   gettimeofday( &timeValue, &timeZone );

   now = (double) (timeValue.tv_sec - boottime) + (timeValue.tv_usec / 1000000.0);

   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "pool_idle_time=" );
   else
      p = (char *) NULL;

   if (p)
   {
      delta_t = now - last_time;

      pool_idle = strtoll( p+15, (char **) NULL, 10 );

      p = strstr( my_update_file( &proc_cpuinfo ), "timebase" );

      if ((delta_t > 0.0) && (p))
      {
         p = strchr( p, ':' );
         p = skip_whitespace( p+1 );

         timebase = strtoll( p, (char **) NULL, 10 );

         pool_idle_diff = pool_idle - pool_idle_saved;

         if ((timebase > 0LL) && (pool_idle_diff >= 0LL))
            val.f = (double) (pool_idle_diff) / (double) timebase / delta_t;
         else
            val.f = last_val;
      }
      else
         val.f = 0.0;

      pool_idle_saved = pool_idle;
   }
   else
      val.f = 0.0;

/* prevent against huge value when suddenly performance data collection */
/* is enabled or disabled for this LPAR */
   if (val.f > MAX_CPU_POOL_IDLE)
      val.f = 0.0;

   last_time = now;
   last_val = val.f;

   return( val );
}



g_val_t
cpu_used_func( void )
{
   g_val_t val;
   static long long purr_saved = 0LL;
   long long purr, purr_diff, timebase;
   static double last_time = 0.0;
   static double last_system_check_time = 0.0;
   static float last_val = 0.0;
   double now, delta_t;
   struct timeval timeValue;
   struct timezone timeZone;
   char *p;
   int cpus;


   gettimeofday( &timeValue, &timeZone );

   now = (double) (timeValue.tv_sec - boottime) + (timeValue.tv_usec / 1000000.0);

/* check every 180 seconds if we are still on the same system --> LPAR Mobility */
   if (now - last_system_check_time >= 180.0)
   {
      CheckPURRusability();
      last_system_check_time = now;
   }

   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "purr=" );
   else
      p = (char *) NULL;

   if (p && purrUsable)
   {
      delta_t = now - last_time;

      purr = strtoll( p+5, (char **) NULL, 10 );

      p = strstr( my_update_file( &proc_cpuinfo ), "timebase" );

      if ((delta_t > 0.0) && (p))
      {
         p = strchr( p, ':' );
         p = skip_whitespace( p+1 );

         timebase = strtoll( p, (char **) NULL, 10 );

         purr_diff = purr - purr_saved;

         if ((timebase > 0LL) && (purr_diff >= 0LL))
            val.f = (double) (purr_diff) / (double) timebase / delta_t;
         else
            val.f = last_val;
      }
      else
         val.f = 0.0;

      purr_saved = purr;
   }
   else /* dedicated LPAR/standalone system so calculate cpu_used with cpu_idle_func() */
   {
/* find out number of CPUs in the system/LPAR via /proc/ppc64/lparcfg */
/* --> partition_active_processors should always exist */
      if (LPARcfgExists)
         p = strstr( my_update_file( &proc_ppc64_lparcfg ), "partition_active_processors=" );
      else
         p = (char *) NULL;

      if (p)
      {
         cpus = strtol( p+28, (char **) NULL, 10 );

         val = cpu_idle_func();
         val.f = (float) cpus * (100.0 - val.f) / 100.0;
      }
      else
         val.f = 0.0;
   }

/* sanity check to prevent against accidental huge value */
   if (val.f >= 256.0)
      val.f = 0.0;

   last_time = now;
   last_val = val.f;

/* save value for cpu_ec_func */
   last_cpu_used = val.f;

   return( val );
}



g_val_t
cpu_ec_func( void )
{
   g_val_t ent, val;


   ent = cpu_entitlement_func();

   if (ent.f != 0.0)
      val.f = 100.0 * (last_cpu_used / ent.f);
   else
      val.f = 100.0;

   return( val );
}



struct dsk_stat {
        char          dk_name[32];
        int           dk_major;
        int           dk_minor;
        long          dk_noinfo;
        unsigned long dk_reads;
        unsigned long dk_rmerge;
        unsigned long dk_rmsec;
        unsigned long dk_rkb;
        unsigned long dk_writes;
        unsigned long dk_wmerge;
        unsigned long dk_wmsec;
        unsigned long dk_wkb;
        unsigned long dk_xfers;
        unsigned long dk_bsize;
        unsigned long dk_time;
        unsigned long dk_inflight;
        unsigned long dk_11;
        unsigned long dk_partition;
        unsigned long dk_blocks; /* in /proc/partitions only */
        unsigned long dk_use;
        unsigned long dk_aveq;
};



static void
get_diskstats_iops( double *iops )
{
   char *p, *q;
   char buf[1024];
   int  ret;
   long long total_iops, diff;
   static long long saved_iops = 0LL;
   struct dsk_stat dk;
   static double last_time = 0.0;
   static double last_val = 0.0;
   double now, delta_t;
   struct timeval timeValue;
   struct timezone timeZone;


   gettimeofday( &timeValue, &timeZone );

   now = (double) (timeValue.tv_sec - boottime) + (timeValue.tv_usec / 1000000.0);


   p = my_update_file( &proc_diskstats );

   if (p)
   {
      total_iops = 0LL;

      while ((q = strchr( p, '\n' )))
      {
         /* zero the data ready for reading */
         dk.dk_reads = dk.dk_writes = 0;

         strncpy( buf, p, q-p );
         buf[q-p] = '\0';

         ret = sscanf( buf, "%d %d %s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                       &dk.dk_major,
                       &dk.dk_minor,
                       &dk.dk_name[0],
                       &dk.dk_reads,
                       &dk.dk_rmerge,
                       &dk.dk_rkb,
                       &dk.dk_rmsec,
                       &dk.dk_writes,
                       &dk.dk_wmerge,
                       &dk.dk_wkb,
                       &dk.dk_wmsec,
                       &dk.dk_inflight,
                       &dk.dk_time,
                       &dk.dk_11 );

         p = q+1;

         if (ret == 7)  /* skip partitions of a disk */
           continue;

         if (strncmp(dk.dk_name, "dm-", 3) == 0)
           continue;

         if (strncmp(dk.dk_name, "md", 2) == 0)
           continue;

#ifdef MPERZL_DEBUG
fprintf(stderr, "dk_name = %5s, dk_reads = %10ld, dk_writes = %10ld\n", dk.dk_name, dk.dk_reads, dk.dk_writes);
#endif

         total_iops += dk.dk_reads + dk.dk_writes;
      }

#ifdef MPERZL_DEBUG
fprintf(stderr, "total_iops = %" PRIi64 "\n", total_iops);
fprintf(stderr, "saved_iops = %" PRIi64 "\n", saved_iops);
#endif

      delta_t = now - last_time;

      if (delta_t > 0)
      {
         diff = total_iops - saved_iops;

         if (diff > 0LL)
            *iops = diff / delta_t;
         else
            *iops = 0.0;
      }
      else
         *iops = 0.0;

      saved_iops = total_iops;
   }
   else
   {
      *iops = 0.0;
   }

   last_time = now;
   last_val = *iops;
}



static void
get_diskstats_read( double *read )
{
   char *p, *q;
   char buf[1024];
   int  ret;
   long long total_read, diff;
   static long long saved_read = 0LL;
   struct dsk_stat dk;
   static double last_time = 0.0;
   static double last_val = 0.0;
   double now, delta_t;
   struct timeval timeValue;
   struct timezone timeZone;


   gettimeofday( &timeValue, &timeZone );

   now = (double) (timeValue.tv_sec - boottime) + (timeValue.tv_usec / 1000000.0);


   p = my_update_file( &proc_diskstats );

   if (p)
   {
      total_read  = 0;

      while ((q = strchr( p, '\n' )))
      {
         /* zero the data ready for reading */
         dk.dk_rkb = 0;

         strncpy( buf, p, q-p );
         buf[q-p] = '\0';

         ret = sscanf( buf, "%d %d %s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                       &dk.dk_major,
                       &dk.dk_minor,
                       &dk.dk_name[0],
                       &dk.dk_reads,
                       &dk.dk_rmerge,
                       &dk.dk_rkb,
                       &dk.dk_rmsec,
                       &dk.dk_writes,
                       &dk.dk_wmerge,
                       &dk.dk_wkb,
                       &dk.dk_wmsec,
                       &dk.dk_inflight,
                       &dk.dk_time,
                       &dk.dk_11 );

         p = q+1;

         if (ret == 7)  /* skip partitions of a disk */
            continue;

         if (strncmp(dk.dk_name, "md", 2) == 0)
            continue;

         if (strncmp(dk.dk_name, "dm-", 3) == 0)
            continue;

#ifdef MPERZL_DEBUG
printf("dk_rkb = %ld   dk_wkb = %ld\n", dk.dk_rkb, dk.dk_wkb);
#endif

         dk.dk_rkb /= 2; /* sectors = 512 bytes */

         total_read  += dk.dk_rkb;
      }

#ifdef MPERZL_DEBUG
printf("total_read  = %" PRIi64 "\n", total_read);
printf("saved_read  = %" PRIi64 "\n", saved_read);
#endif

      delta_t = now - last_time;

      if (delta_t > 0)
      {
         diff = total_read - saved_read;

         if (diff > 0LL)
            *read = diff / delta_t;
         else
            *read = 0.0;
      }
      else
         *read = 0.0;

      saved_read  = total_read;
   }
   else
   {
      *read  = 0.0;
   }

   last_time = now;
   last_val = *read;
}



static void
get_diskstats_write( double *write )
{
   char *p, *q;
   char buf[1024];
   int  ret;
   long long total_write, diff;
   static long long saved_write = 0LL;
   struct dsk_stat dk;
   static double last_time = 0.0;
   static double last_val = 0.0;
   double now, delta_t;
   struct timeval timeValue;
   struct timezone timeZone;


   gettimeofday( &timeValue, &timeZone );

   now = (double) (timeValue.tv_sec - boottime) + (timeValue.tv_usec / 1000000.0);


   p = my_update_file( &proc_diskstats );

   if (p)
   {
      total_write = 0;

      while ((q = strchr( p, '\n' )))
      {
         /* zero the data ready for reading */
         dk.dk_wkb = 0;

         strncpy( buf, p, q-p );
         buf[q-p] = '\0';

         ret = sscanf( buf, "%d %d %s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                       &dk.dk_major,
                       &dk.dk_minor,
                       &dk.dk_name[0],
                       &dk.dk_reads,
                       &dk.dk_rmerge,
                       &dk.dk_rkb,
                       &dk.dk_rmsec,
                       &dk.dk_writes,
                       &dk.dk_wmerge,
                       &dk.dk_wkb,
                       &dk.dk_wmsec,
                       &dk.dk_inflight,
                       &dk.dk_time,
                       &dk.dk_11 );

         p = q+1;

         if (ret == 7)  /* skip partitions of a disk */
            continue;

         if (strncmp(dk.dk_name, "md", 2) == 0)
            continue;

         if (strncmp(dk.dk_name, "dm-", 3) == 0)
            continue;

#ifdef MPERZL_DEBUG
printf("dk_rkb = %ld   dk_wkb = %ld\n", dk.dk_rkb, dk.dk_wkb);
#endif

         dk.dk_wkb /= 2; /* sectors = 512 bytes */

         total_write += dk.dk_wkb;
      }

#ifdef MPERZL_DEBUG
printf("total_write = %" PRIi64 "\n", total_write);
printf("saved_write = %" PRIi64 "\n", saved_write);
#endif

      delta_t = now - last_time;

      if (delta_t > 0)
      {
         diff = total_write - saved_write;

         if (diff > 0LL)
            *write = diff / delta_t;
         else
            *write = 0.0;
      }
      else
         *write = 0.0;

      saved_write = total_write;
   }
   else
   {
      *write = 0.0;
   }

   last_time = now;
   last_val = *write;
}



g_val_t
disk_iops_func( void )
{
   g_val_t val;
   double disk_iops;

 
   get_diskstats_iops( &disk_iops );

   val.d = disk_iops;

   return( val );
}



g_val_t
disk_read_func( void )
{
   g_val_t val;
   double disk_read;

 
   get_diskstats_read( &disk_read );

   val.d = disk_read * 1024.0;

   return( val );
}



g_val_t
disk_write_func( void )
{
   g_val_t val;
   double disk_write;

 
   get_diskstats_write( &disk_write );

   val.d = disk_write * 1024.0;

   return( val );
}



g_val_t
fwversion_func( void )
{
   FILE    *f;
   g_val_t  val;
   char     buf1[MAX_G_STRING_SIZE],
            buf2[MAX_G_STRING_SIZE];


   strcpy( val.str, "Firmware version not detected!" );

   f = fopen( "/proc/device-tree/openprom/ibm,fw-vernum_encoded", "r" );

   if (f)
   {
      if (fread( val.str, 1, MAX_G_STRING_SIZE, f ) > 0)
         val.str[MAX_G_STRING_SIZE - 1] = '\0';  /* truncate to MAX_G_STRING_SIZE */

      fclose( f );
   }
   else
   {
      memset( buf1, '\0', MAX_G_STRING_SIZE );
      memset( buf2, '\0', MAX_G_STRING_SIZE );

      f = popen( "cat /proc/device-tree/ibm,opal/firmware/ml-version | awk '{ print $2 }'", "r" );
      if (f)
      {
         if (fgets( buf1, MAX_G_STRING_SIZE, f ) > 0)
         {
            buf1[MAX_G_STRING_SIZE - 1] = '\0';  /* truncate to MAX_G_STRING_SIZE */
            buf1[strlen( buf1 ) - 1] = '\0';  /* replace \n */
         }

         pclose( f );
      }

      f = popen( "cat /proc/device-tree/ibm,opal/firmware/mi-version | awk '{ print $2 }'", "r" );
      if (f)
      {
         if (fgets( buf2, MAX_G_STRING_SIZE, f ) > 0)
         {
            buf2[MAX_G_STRING_SIZE - 1] = '\0';  /* truncate to MAX_G_STRING_SIZE */
            buf2[strlen( buf2 ) - 1] = '\0';  /* replace \n */
         }

         pclose( f );
      }

      if ((strlen( buf1 ) > 1) &&
          (strlen( buf2 ) > 1) &&
          (strlen( buf1 ) + strlen( buf2 ) < MAX_G_STRING_SIZE - 3))
      {
         strcpy( val.str, buf1 );
         strcat( val.str, " (" );
         strcat( val.str, buf2 );
         strcat( val.str, ")" );
      }
   }

   return( val );
}



static int LinuxVersion = 0;    /* 1 = /etc/SuSE-release */
                                /* 2 = /etc/redhat-release */
                                /* 3 = /etc/os-release */
                                /* 4 = /etc/debian_version */



/* find 64bit kernel or not */
g_val_t
kernel64bit_func( void )
{
   g_val_t  val;
   FILE    *f;
   char     buf[128];
   int      kernel64bit=0, i;


   if ((LinuxVersion == 1) || (LinuxVersion == 2))
      f = popen( "uname -i 2>/dev/null", "r" );
   else
      if ((LinuxVersion == 3) || (LinuxVersion == 4))  /* Debian has no "uname -i" */
         f = popen( "uname -m 2>/dev/null", "r" );
      else
         f = popen( "uname -r 2>/dev/null", "r" );

   if (f == NULL)
      strcpy( val.str, "popen() of 'uname -[i,m,r]' failed" );
   else
   {
      if (fread( buf, 1, 128, f ) > 0)
      {
         buf[127] = '\0';

         for (i = 0;  buf[i] != '\0';  i++)
         {
            if (buf[i] == '6' && buf[i+1] == '4')
            {
               kernel64bit++;
               break;
            }
         }

         strcpy( val.str, kernel64bit ? "yes" : "no" );
      }
      else
         strcpy( val.str, "popen() of 'uname -[i,m,r]' failed" );

      pclose( f );
   }

   return( val );
}



g_val_t
lpar_func( void )
{
   g_val_t val;
   char *p;
   int capped, shared_processor_mode, partition_id;
   long DisWheRotPer;
   long long purr;


   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "shared_processor_mode=" );
   else
      p = (char *) NULL;
   if (p)
      shared_processor_mode = strtol( p+22, (char **) NULL, 10 );
   else
      shared_processor_mode = -1;

   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "capped=" );
   else
      p = (char *) NULL;
   if (p)
      capped = strtol( p+7, (char **) NULL, 10 );
   else
      capped = -1;

   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "partition_id=" );
   else
      p = (char *) NULL;
   if (p)
      partition_id = strtol( p+13, (char **) NULL, 10 );
   else
      partition_id = -1;

   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "DisWheRotPer=" );
   else
      p = (char *) NULL;
   if (p)
      DisWheRotPer = strtol( p+13, (char **) NULL, 10 );
   else
      DisWheRotPer = -1;

   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "purr=" );
   else
      p = (char *) NULL;
   if (p)
      purr = strtoll( p+5, (char **) NULL, 10 );
   else
      purr = -1;


   if (shared_processor_mode > 0 ||
       capped >= 0 ||
       partition_id > 0 ||
       DisWheRotPer > 0 ||
       purr > 0)
      strcpy( val.str, "yes" );
   else
      strcpy( val.str, "no" );


   return( val );
}



g_val_t
lpar_name_func( void )
{
   g_val_t val;
   FILE *f;
   char buf[128];


   f = fopen( "/proc/device-tree/ibm,partition-name", "r" );

   if (f)
   {
      if (fgets( buf, 128, f ) )
      {
         if (strlen( buf ) > MAX_G_STRING_SIZE - 1)
            buf[MAX_G_STRING_SIZE - 1] = '\0';

         strcpy( val.str, buf );
      }
      else
         strcpy( val.str, "Can't find out LPAR name!" );

      fclose( f );
   }
   else
      strcpy( val.str, "No LPAR system" );

   return( val );
}



g_val_t
lpar_num_func( void )
{
   g_val_t val;
   char *p;


   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "partition_id=" );
   else
      p = (char *) NULL;

   if (p)
      val.int32 = strtol( p+13, (char **) NULL, 10 );
   else
      val.int32 = -1;

   return( val );
}



g_val_t
model_name_func( void )
{
   g_val_t val;
   FILE *f;
   char *p;
   int len;


   if (LPARcfgExists)
   {
      if (KVM_Guest)
      {
         f = fopen( "/proc/device-tree/host-model", "r" );

         if (f)
         {
            if (fread( val.str, 1, MAX_G_STRING_SIZE, f ) > 0)
            {
               val.str[MAX_G_STRING_SIZE - 1] = '\0';  /* truncate to MAX_G_STRING_SIZE */
               val.str[strlen( val.str )] = '\0';
            }

            fclose( f );
         }
         else
            strcpy( val.str, "KVM Guest" );
      }
      else
      {
         p = strstr( my_update_file( &proc_ppc64_lparcfg ), "system_type=" );

         if (p)
         {
            len = strchr( p+12, '\n' ) - (p+12);
            if (len > MAX_G_STRING_SIZE - 1)
               len = MAX_G_STRING_SIZE - 1;
            strncpy( val.str, p+12, len );
            val.str[len] = '\0';
         }
         else
            strcpy( val.str, "Can't find out model name" );
      }
   }
   else
   {
      p = strstr( my_update_file( &proc_cpuinfo ), "model" );

      if (p)
      {
         p = strchr( p, ':' );
         p = skip_whitespace( p+1 );

         len = strchr( p, '\n' ) - p;
         if (len > MAX_G_STRING_SIZE - 1)
            len = MAX_G_STRING_SIZE - 1;
         strncpy( val.str, p, len );
         val.str[len] = '\0';
      }
      else
         strcpy( val.str, "Can't find out model name" );
   }

   return( val );
}



/* find OS version just once */
static g_val_t
oslevel_func_CALLED_ONCE( void )
{
   g_val_t  val;
   FILE    *f, *f2;
   char     buf[256], *p, *q;
   int      i;


   f = fopen( "/etc/SuSE-release", "r" );
   if (f) LinuxVersion = 1;
   if (! LinuxVersion)
   {
      f = fopen( "/etc/redhat-release", "r" );
      if (f) LinuxVersion = 2;
   }
   if (! LinuxVersion)
   {
      f = fopen( "/etc/os-release", "r" );
      if (f) LinuxVersion = 3;
   }
   if (! LinuxVersion)
   {
      f = fopen( "/etc/debian_version", "r" );
      if (f) LinuxVersion = 4;
   }
   if (f == NULL)
      strcpy( val.str, "No known Linux release found" );
   else
   {
      if (fread( buf, 1, 256, f) > 0)
      {
         if (LinuxVersion == 1)
         {
            if ((! strncmp( buf, "SUSE LINUX Enterprise Server", 28 )) ||
                (! strncmp( buf, "SUSE Linux Enterprise Server", 28 )))
            {
               strcpy( val.str, "SLES " );

               p = strchr( buf, '\n' );
               if (p) p = strchr( p+1, '=' );
               if (p) p = skip_whitespace( p+1 );
               if (p) q = strchr( p, '\n' );

               if (p && q) strncat( val.str, p, q-p );
               strcat( val.str, " SP " );

               if (q) p = strchr( q+1, '=' );
               if (p) p = skip_whitespace( p+1 );
               if (p) q = strchr( p, '\n' );

               if (p && q) strncat( val.str, p, q-p );
            }
            else
            {
               p = strchr( buf, '\n' );

               i = p-buf;
               if ((i < 0) || (i >= MAX_G_STRING_SIZE))
                  i = MAX_G_STRING_SIZE - 1;

               strncpy( val.str, buf, i );
               val.str[i] = '\0';
            }
         }
         else if (LinuxVersion == 2)
         {
            if (! strncmp( buf, "Red Hat Enterprise Linux AS release", 35 ))
            {
               strcpy( val.str, "Red Hat Enterprise Linux " );

               p = skip_whitespace( buf+35 );
               if (p) q = strchr( p, ' ' );

               if (p && q) strncat( val.str, p, q-p );

               if (q) p = strstr( q+1, "Update " );

               if (p)
               {
                  strcat( val.str, " Update " );

                  p = skip_whitespace( p+7 );
                  if (p) q = strchr( p, ')' );

                  if (p && q) strncat( val.str, p, q-p );
               }
            }
            else
            if (! strncmp( buf, "Red Hat Enterprise Linux Server release", 39 ))
            {
               strcpy( val.str, "Red Hat Enterprise Linux " );

               p = skip_whitespace( buf+39 );
               if (p) q = strchr( p, ' ' );

               if (p && q) strncat( val.str, p, q-p );
            }
            else
            {
               p = strchr( buf, '\n' );

               i = p-buf;
               if ((i < 0) || (i >= MAX_G_STRING_SIZE))
                  i = MAX_G_STRING_SIZE - 1;

               strncpy( val.str, buf, i );
               val.str[i] = '\0';
            }
         }
         else if (LinuxVersion == 3)
         {
            f2 = popen( "cat /etc/os-release | egrep '^NAME=|^VERSION=' | sed 's/NAME=//g' | sed 's/\"//g' | sed 's/VERSION=//g' 2>/dev/null", "r" );

            if (f2)
            {
               if (fread( buf, 1, 256, f2) > 0)
               {
                  p = strchr( buf, '\n' );
                  i = p-buf;
                  if ((i < 0) || (i >= MAX_G_STRING_SIZE))
                     i = MAX_G_STRING_SIZE - 1;
                  buf[i] = ' ';

                  p = strchr( p+1, '\n' );
                  i = p-buf;
                  if ((i < 0) || (i >= MAX_G_STRING_SIZE))
                     i = MAX_G_STRING_SIZE - 1;

                  strncpy( val.str, buf, i );
                  val.str[i] = '\0';
                  
                  pclose( f2 );
               }
            }
            else
               strcpy( val.str, "Couldn't read /etc/os-release" );
         }
         else if (LinuxVersion == 4)
         {
            p = strchr( buf, '\n' );

            i = p-buf;
            if ((i < 0) || (i >= MAX_G_STRING_SIZE))
               i = MAX_G_STRING_SIZE - 1;

            strncpy( val.str, buf, i );
            val.str[i] = '\0';
         }
      }
      else
         strcpy( val.str, "No known Linux release found" );

      fclose( f );
   }

   return( val );
}



g_val_t
oslevel_func( void )
{
   static g_val_t val;
   static int firstTime = TRUE;


   if (firstTime)
   {
      val = oslevel_func_CALLED_ONCE();
      firstTime = FALSE;
   }

   return( val );
}



g_val_t
serial_num_func( void )
{
   g_val_t val;
   FILE *f;
   char  buf[128],
         *p;
   int len;


   strcpy( val.str, "serial number not found" );

   if (KVM_Guest)
   {
      f = fopen( "/proc/device-tree/host-serial", "r" );

      if (f)
      {
         if (fgets( buf, 128, f ) )
         {
            if (strlen( buf ) > MAX_G_STRING_SIZE - 1)
               buf[MAX_G_STRING_SIZE - 1] = '\0';

            strcpy( val.str, buf );
         }

         fclose( f );
      }
   }
   else
   {
      f = fopen( "/proc/device-tree/system-id", "r" );

      if (f)
      {
         if (fgets( buf, 128, f ) )
         {
            if (strlen( buf ) > MAX_G_STRING_SIZE - 1)
               buf[MAX_G_STRING_SIZE - 1] = '\0';

            strcpy( val.str, buf );
         }

         fclose( f );
      }
      else
         if (LPARcfgExists)
         {
            p = strstr( my_update_file( &proc_ppc64_lparcfg), "serial_number=" );

            if (p)
            {
               len = strchr( p+14, '\n' ) - (p+14);
               if (len > MAX_G_STRING_SIZE - 1)
                  len = MAX_G_STRING_SIZE - 1;
               strncpy( val.str, p+14, len );
               val.str[len] = '\0';
            }
         }
   }

   return( val );
}



g_val_t
smt_func( void )
{
   g_val_t val;
   char *p;
   int i, virtCPUcount;


   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "shared_processor_mode=" );
   else
      p = (char *) NULL;

   if (p)
   {
      i = strtol( p+22, (char **) NULL, 10 );

      if (i > 0)
      {
      }
   }
   else
      strcpy( val.str, "No SPLPAR-capable system" );


   p = my_update_file( &proc_stat );

/* Skip initial "cpu" token and find first real cpu "cpu0" */
   p = strstr( p+3, "cpu");

   i = 1;
   while ((p = strstr( p+3, "cpu" )))
      i++;

   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "partition_active_processors=" );
   else
      p = (char *) NULL;

   if (p)
   {
      virtCPUcount = strtol( p+28, (char **) NULL, 10 );
      if (i > virtCPUcount)
         snprintf( val.str, MAX_G_STRING_SIZE, "yes (SMT=%d)", i / virtCPUcount );
      else
         strcpy( val.str, "no (SMT=1)" );
   }
   else
      strcpy( val.str, "No SMT-capable system" );

   return( val );
}



g_val_t
splpar_func( void )
{
   g_val_t val;
   char *p;


   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "shared_processor_mode=" );
   else
      p = (char *) NULL;

   if (p)
      strcpy( val.str, strtol( p+22, (char **) NULL, 10 ) == 1 ? "yes" : "no" );
   else
      strcpy( val.str, "No SPLPAR-capable system" );

   return( val );
}



g_val_t
weight_func( void )
{
   g_val_t val;
   char *p;


   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "unallocated_capacity_weight=" );
   else
      p = (char *) NULL;

   if (p)
   {
      p = strstr( p+29, "capacity_weight=" );

      if (p)
         val.int32 = strtol( p+16, (char **) NULL, 10 );
      else
         val.int32 = -1;
   }
   else
      val.int32 = -1;

   return( val );
}



g_val_t
kvm_guest_func( void )
{
   g_val_t val;


   if (KVM_Guest)
      strcpy( val.str, "yes" );    
   else
      strcpy( val.str, "no" );    

   return( val );
}



g_val_t
cpu_type_func( void )
{
   g_val_t  val;
   char    *p, *q;
   long long l;


   strcpy( val.str, "Unknown" );

   if (KVM_Guest)
   {
      p = strstr( my_update_file( &proc_cpuinfo ), "model" );
      if (p) p = strchr( p+1, ':' );
      if (p) p = skip_whitespace( p+1 );
      if (p) q = strchr( p, '\n' );
      if (p && q)
      {
         l = q - p;
         if (l > MAX_G_STRING_SIZE - 1)
            l = MAX_G_STRING_SIZE - 1;

         strncpy( val.str, p, l );
         val.str[l] = '\0';
      }
   }
   else
   {
      p = strstr( my_update_file( &proc_cpuinfo ), "cpu" );
      if (p) p = strchr( p+1, ':' );
      if (p) p = skip_whitespace( p+1 );
      if (p) q = strchr( p, '\n' );
      if (p && q)
      {
         l = q - p;
         if (l > MAX_G_STRING_SIZE - 1)
            l = MAX_G_STRING_SIZE - 1;

         strncpy( val.str, p, l );
         val.str[l] = '\0';
      }
   }

   return( val );
}



static int
Running_as_KVM_Guest( void )
{
   char *p,
         buf[128];
   int len;


   if (LPARcfgExists)
   {
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "system_type=" );

      strcpy( buf, "" );
      if (p)
      {
         len = strchr( p+12, '\n' ) - (p+12);
         if (len > 127)
            len = 127;
         strncpy( buf, p+12, len );
         buf[len] = '\0';
      }

      if (! strcmp( buf, "IBM pSeries (emulated by qemu)" ) )
         return( TRUE );
   }

   return( FALSE );
}



static int
Running_as_SPLPAR( void )
{
   char *p;
   int i;


   if (LPARcfgExists)
      p = strstr( my_update_file( &proc_ppc64_lparcfg ), "shared_processor_mode=" );
   else
      p = (char *) NULL;

   if (p)
      i = strtol( p+22, (char **) NULL, 10 ) > 0;
   else
      i = FALSE;

   return( i );
}



/*
 * Declare ourselves so the configuration routines can find and know us.
 * We'll fill it in at the end of the module.
 */
extern mmodule ibmpower_module;


static int
ibmpower_metric_init ( apr_pool_t *p )
{
   int i;
   g_val_t val;


   for (i = 0;  ibmpower_module.metrics_info[i].name != NULL;  i++)
   {
      /* Initialize the metadata storage for each of the metrics and then
       *  store one or more key/value pairs.  The define MGROUPS defines
       *  the key for the grouping attribute. */
      MMETRIC_INIT_METADATA( &(ibmpower_module.metrics_info[i]), p );
      MMETRIC_ADD_METADATA( &(ibmpower_module.metrics_info[i]), MGROUP, "ibmpower" );
   }


/* determine if we are running in OPAL or pHyp mode, KVM guest or not etc. */

   LPARcfgExists = my_fileexists( "/proc/ppc64/lparcfg" );

   KVM_Guest = Running_as_KVM_Guest();

   if (KVM_Guest)
      KVM_Mode = 1;
   else
      if (! LPARcfgExists)
         KVM_Mode = 2;
      else
         KVM_Mode = 0;

   SPLPAR_Mode = Running_as_SPLPAR();


/* initialize the routines which require a time interval */

   boottime = boottime_func_CALLED_ONCE();

   CheckPURRusability();

   val = oslevel_func();
   val = cpu_pool_idle_func();
   val = cpu_used_func();
   val = disk_iops_func();
   val = disk_read_func();
   val = disk_write_func();


/* return SUCCESS */

   return( 0 );
}



static void
ibmpower_metric_cleanup ( void )
{
}



static g_val_t
ibmpower_metric_handler ( int metric_index )
{
   g_val_t val;

/* The metric_index corresponds to the order in which
   the metrics appear in the metric_info array
*/
   switch (metric_index)
   {
      case 0:  return( capped_func() );
      case 1:  return( cpu_ec_func() );
      case 2:  return( cpu_entitlement_func() );
      case 3:  return( cpu_in_lpar_func() );
      case 4:  return( cpu_in_machine_func() );
      case 5:  return( cpu_in_pool_func() );
      case 6:  return( cpu_in_syspool_func() );
      case 7:  return( cpu_pool_id_func() );
      case 8:  return( cpu_pool_idle_func() );
      case 9:  return( cpu_used_func() );
      case 10: return( disk_iops_func() );
      case 11: return( disk_read_func() );
      case 12: return( disk_write_func() );
      case 13: return( fwversion_func() );
      case 14: return( kernel64bit_func() );
      case 15: return( lpar_func() );
      case 16: return( lpar_name_func() );
      case 17: return( lpar_num_func() );
      case 18: return( model_name_func() );
      case 19: return( oslevel_func() );
      case 20: return( serial_num_func() );
      case 21: return( smt_func() );
      case 22: return( splpar_func() );
      case 23: return( weight_func() );
      case 24: return( kvm_guest_func() );
      case 25: return( cpu_type_func() );
      default: val.uint32 = 0; /* default fallback */
   }

   return( val );
}



static Ganglia_25metric ibmpower_metric_info[] = 
{
   {0, "capped",           180, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+64, "Is this SPLPAR running in capped mode?"},
   {0, "cpu_ec",            15, GANGLIA_VALUE_FLOAT,        "%",    "both", "%.2f", UDP_HEADER_SIZE+8,  "Ratio of physical cores used vs. entitlement"},
   {0, "cpu_entitlement",  180, GANGLIA_VALUE_FLOAT,        "CPUs", "both", "%.2f", UDP_HEADER_SIZE+8,  "Capacity entitlement in units of physical cores"},
   {0, "cpu_in_lpar",      180, GANGLIA_VALUE_UNSIGNED_INT, "CPUs", "both", "%d",   UDP_HEADER_SIZE+8,  "Number of CPUs the OS sees in the system"},
   {0, "cpu_in_machine",  1200, GANGLIA_VALUE_UNSIGNED_INT, "CPUs", "both", "%d",   UDP_HEADER_SIZE+8,  "Total number of physical cores in the whole system"},
   {0, "cpu_in_pool",      180, GANGLIA_VALUE_UNSIGNED_INT, "CPUs", "both", "%d",   UDP_HEADER_SIZE+8,  "Number of physical cores in the shared processor pool"},
   {0, "cpu_in_syspool",   180, GANGLIA_VALUE_UNSIGNED_INT, "CPUs", "both", "%d",   UDP_HEADER_SIZE+8,  "Number of physical cores in the global shared processor pool"},
   {0, "cpu_pool_id",      180, GANGLIA_VALUE_UNSIGNED_INT, "",     "both", "%d",   UDP_HEADER_SIZE+8,  "Shared processor pool ID of this LPAR"},
   {0, "cpu_pool_idle",     15, GANGLIA_VALUE_FLOAT,        "CPUs", "both", "%.4f", UDP_HEADER_SIZE+8,  "Number of idle cores in the shared processor pool"},
   {0, "cpu_used",          15, GANGLIA_VALUE_FLOAT,        "CPUs", "both", "%.4f", UDP_HEADER_SIZE+8,  "Number of physical cores used"},
   {0, "disk_iops",        180, GANGLIA_VALUE_DOUBLE,     "IO/sec", "both", "%.3f", UDP_HEADER_SIZE+16, "Total number of I/O operations per second"},
   {0, "disk_read",        180, GANGLIA_VALUE_DOUBLE,  "bytes/sec", "both", "%.2f", UDP_HEADER_SIZE+16, "Total number of bytes read I/O of the system"},
   {0, "disk_write",       180, GANGLIA_VALUE_DOUBLE,  "bytes/sec", "both", "%.2f", UDP_HEADER_SIZE+16, "Total number of bytes write I/O of the system"},
   {0, "fwversion",       1200, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+64, "Firmware Version"},
   {0, "kernel64bit",     1200, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+64, "Is the kernel running in 64-bit mode?"},
   {0, "lpar",            1200, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+64, "Is the system an LPAR or not?"},
   {0, "lpar_name",        180, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+64, "Name of the LPAR as defined on the HMC"},
   {0, "lpar_num",        1200, GANGLIA_VALUE_UNSIGNED_INT, "",     "both", "%d",   UDP_HEADER_SIZE+8,  "Partition ID of the LPAR as defined on the HMC"},
   {0, "model_name",      1200, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+64, "Machine Model Name"},
   {0, "oslevel",          180, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+64, "Exact Linux version"},
   {0, "serial_num",      1200, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+64, "Serial number of the hardware system"},
   {0, "smt",              180, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+64, "Is SMT enabled or not?"},
   {0, "splpar",          1200, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+64, "Is this a shared processor LPAR or not?"},
   {0, "weight",           180, GANGLIA_VALUE_UNSIGNED_INT, "",     "both", "%d",   UDP_HEADER_SIZE+8,  "Capacity weight of the LPAR"},
   {0, "kvm_guest",       1200, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+64, "Is this a KVM guest VM or not?"},
   {0, "cpu_type",         180, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+64, "CPU model name"},
   {0, NULL}
};



mmodule ibmpower_module =
{
   STD_MMODULE_STUFF,
   ibmpower_metric_init,
   ibmpower_metric_cleanup,
   ibmpower_metric_info,
   ibmpower_metric_handler,
};

