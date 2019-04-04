/******************************************************************************
 *
 *  This module implements IBM POWER5/6/7/8-specific extensions like:
 *    - SPLPAR
 *    - SMT
 *    - CPU Entitlement
 *    - Capped/Uncapped
 *    - etc.
 *
 *  The libperfstat API is used and it can deal with a 32-bit and a 64-bit
 *  kernel and does not require root authority.
 *
 *  The code has been tested with AIX 5.1, 5.2, 5.3, 6.1, 7.1 and 7.2
 *  on different systems.
 *
 *  Written by Michael Perzl (michael@perzl.org)
 *
 *  Version 1.6, Oct 26, 2017
 *
 *  based on Ganglia V3.0.7 AIX Ganglia libmetrics code written by:
 *         Michael Perzl (michael@perzl.org)
 *     and Nigel Griffiths (nigelargriffiths@hotmail.com)
 *
 *  Version 1.6:  Oct 26, 2017
 *                - added defines for AIX 7.2
 *                - added KVM Guest detection
 *                  (--> kvm_guest_func() )
 *                - added CPU type detection
 *                  (--> cpu_type_func() )
 *                - fixed SMT detection
 *                  (--> smt_func() )
 *
 *  Version 1.5:  Jun 10, 2013
 *                - adapted to new AIX libmetrics file
 *                  (--> changed cpu_used_func() )
 *                - added new metric cpu_ec
 *                  (--> cpu_ec_func() )
 *                - improved SMT detection
 *                  (--> smt_func() )
 *
 *  Version 1.4:  Feb 09, 2012
 *                - added new metric cpu_pool_id
 *                  (--> cpu_pool_id_func() )
 *
 *  Version 1.3:  Apr 27, 2010
 *                - added sanity check for cpu_pool_idle_func()
 *                - added new metric fwversion
 *                  (--> fwversion_func() )
 *
 *  Version 1.2:  Feb 10, 2010
 *                - added IO ops/sec metric
 *                  (--> disk_iops_func() )
 *                - changed metric type from GANGLIA_VALUE_FLOAT to
 *                  GANGLIA_VALUE_DOUBLE and changed unit to bytes/sec
 *                  for disk_read_func() and disk_write_func()
 *                - added model_name metric
 *                  (--> model_name_func() )
 *
 *  Version 1.1:  Jan 21, 2010
 *                - improved cpu_used() function
 *                - fixed defuncts caused by open pipes
 *                  (--> popen() without pclose() )
 *                - added checks for possible libperfstat counter resets in
 *                  * cpu_pool_idle_func()
 *                  * cpu_used_func()
 *                  * disk_read_func()
 *                  * disk_write_func()
 *
 *  Version 1.0:  Dec 11, 2008
 *                - initial version
 *
 ******************************************************************************/

/*
 * The ganglia metric "C" interface, required for building DSO modules.
 */

#include <gm_metric.h>


#include <stdlib.h>
#include <strings.h>
#include <time.h>

#include <ctype.h>
#include <utmp.h>
#include <stdio.h>
#include <sys/types.h>
#include <procinfo.h>
#include <signal.h>
#include <odmi.h>
#include <cf.h>
#include <sys/utsname.h>

#if !defined(_AIX43)
#include <sys/dr.h>
#endif
#include <sys/systemcfg.h>

#include <libperfstat.h>

#include "libmetrics.h"


static int isVIOserver;

static time_t boottime;


g_val_t
capped_func( void )
{
   g_val_t val;
#if defined(_AIX53) || defined(_AIX61) || defined(_AIX71) || defined(_AIX72)
   perfstat_partition_total_t p;


   if (perfstat_partition_total( NULL, &p, sizeof( perfstat_partition_total_t ), 1 ) == -1)
      strcpy( val.str, "libperfstat returned an error" );
   else
      if ( __LPAR() && p.type.b.shared_enabled )
         strcpy ( val.str, p.type.b.capped ? "yes" : "no" );
      else
         strcpy( val.str, "No SPLPAR-capable system" );
#else
   strcpy( val.str, "No SPLPAR-capable system" );
#endif

   return( val );
}



g_val_t
cpu_ec_func( void )
{
   g_val_t val;


   val = cpu_entc_func();

   return( val );
}



g_val_t
cpu_entitlement_func( void )
{
   g_val_t val;
#if defined(_AIX53) || defined(_AIX61) || defined(_AIX71) || defined(_AIX72)
   perfstat_partition_total_t p;


   if (perfstat_partition_total( NULL, &p, sizeof( perfstat_partition_total_t ), 1 ) == -1)
      val.f = 0.0;
   else
      if (p.type.b.shared_enabled
#ifdef DONATE_ENABLED
           || p.type.b.donate_enabled
#endif
         )
      {
         val.f = p.entitled_proc_capacity / 100.0;
      }
      else /* dedicated LPAR/standalone system so fake entitled as number of online CPUs */
         val.f = p.online_cpus;
#else
   perfstat_cpu_total_t c;


   if (perfstat_cpu_total( NULL, &c, sizeof( perfstat_cpu_total_t ), 1 ) == -1)
      val.f = 0.;
   else
      val.f = c.ncpus_cfg;
#endif

   return( val );
}



g_val_t
cpu_in_lpar_func( void )
{
   g_val_t val;
#if defined(_AIX53) || defined(_AIX61) || defined(_AIX71) || defined(_AIX72)
   perfstat_partition_total_t p;


   if (perfstat_partition_total( NULL, &p, sizeof( perfstat_partition_total_t ), 1 ) == -1)
      val.int32 = -1;
   else
      val.int32 = p.online_cpus;
#else
   perfstat_cpu_total_t c;


   if (perfstat_cpu_total( NULL, &c, sizeof( perfstat_cpu_total_t ), 1 ) == -1)
      val.int32 = -1;
   else
      val.int32 = c.ncpus_cfg;
#endif

   return( val );
}



g_val_t
cpu_in_machine_func( void )
{
   g_val_t val;
#if defined(_AIX53) || defined(_AIX61) || defined(_AIX71) || defined(_AIX72)
   perfstat_partition_total_t p;


   if (perfstat_partition_total( NULL, &p, sizeof( perfstat_partition_total_t ), 1 ) == -1)
      val.int32 = -1;
   else
      val.int32 = p.online_phys_cpus_sys;
#else
   perfstat_cpu_total_t c;


   if (perfstat_cpu_total( NULL, &c, sizeof( perfstat_cpu_total_t ), 1 ) == -1)
      val.int32 = -1;
   else
      val.int32 = c.ncpus;
#endif

   return( val );
}



g_val_t
cpu_in_pool_func( void )
{
   g_val_t val;
#if defined(_AIX53) || defined(_AIX61) || defined(_AIX71) || defined(_AIX72)
   perfstat_partition_total_t p;


   if (perfstat_partition_total( NULL, &p, sizeof( perfstat_partition_total_t ), 1 ) == -1)
      val.int32 = -1;
   else
      val.int32 = p.phys_cpus_pool;
#else
   perfstat_cpu_total_t c;


   if (perfstat_cpu_total( NULL, &c, sizeof( perfstat_cpu_total_t ), 1 ) == -1)
      val.int32 = -1;
   else
      val.int32 = c.ncpus;
#endif

   return( val );
}



g_val_t
cpu_in_syspool_func( void )
{
   g_val_t val;
#if defined(_AIX53) || defined(_AIX61) || defined(_AIX71) || defined(_AIX72)
   perfstat_partition_total_t p;


   if (perfstat_partition_total( NULL, &p, sizeof( perfstat_partition_total_t ), 1 ) == -1)
      val.int32 = -1;
   else
#if defined(POWER6_POOLS)
   {
      val.int32 = p.shcpus_in_sys;

      if ((val.int32 == 0) && (p.phys_cpus_pool > 0))
         val.int32 = p.phys_cpus_pool;
   }
#else
      val.int32 = p.phys_cpus_pool;
#endif
#else
   perfstat_cpu_total_t c;


   if (perfstat_cpu_total( NULL, &c, sizeof( perfstat_cpu_total_t ), 1 ) == -1)
      val.int32 = -1;
   else
      val.int32 = c.ncpus;
#endif

   return( val );
}



g_val_t
cpu_pool_id_func( void )
{
   g_val_t val;
#if defined(_AIX53) || defined(_AIX61) || defined(_AIX71) || defined(_AIX72)
   perfstat_partition_total_t p;


   if ( __LPAR() )
   {
      if (perfstat_partition_total( NULL, &p, sizeof( perfstat_partition_total_t ), 1) == -1)
         val.int32 = -1;
      else
         val.int32 = p.pool_id;
   }
   else
      val.int32 = -1;
#else
      val.int32 = -1;
#endif

   return( val );
}



#define MAX_CPU_POOL_IDLE (256.0)

g_val_t
cpu_pool_idle_func( void )
{
   g_val_t val;
#if defined(_AIX53) || defined(_AIX61) || defined(_AIX71) || defined(_AIX72)
   perfstat_partition_total_t p;
   lpar_info_format2_t f2;
   static uint64_t saved_pool_idle_time = 0LL;
   longlong_t diff;
   static double last_time = 0.0;
   static float last_val = 0.0;
   double now, delta_t;
   struct timeval timeValue;
   struct timezone timeZone;


   gettimeofday( &timeValue, &timeZone );

   now = (double) (timeValue.tv_sec - boottime) + (timeValue.tv_usec / 1000000.0);
 
   lpar_get_info( LPAR_INFO_FORMAT2, &f2, sizeof( lpar_info_format2_t ) );

   if (perfstat_partition_total( NULL, &p, sizeof( perfstat_partition_total_t ), 1 ) == -1)
      val.f = 0.0;
   else
   {
      delta_t = now - last_time;

      if ( p.type.b.shared_enabled )
      {
         if ( (delta_t > 0.0) && (f2.lpar_flags & LPAR_INFO2_AUTH_PIC) )
         {
            diff = f2.pool_idle_time - saved_pool_idle_time;

            if (diff >= 0LL)
               val.f = (double) diff / delta_t / 1000.0 / 1000.0 / 1000.0;
            else
               val.f = last_val;
         }
         else
            val.f = 0.0;

         saved_pool_idle_time = f2.pool_idle_time;
      }
      else
         val.f = 0.0;
   }

/* prevent against huge value when suddenly performance data collection */
/* is enabled or disabled for this LPAR */
   if (val.f > MAX_CPU_POOL_IDLE)
      val.f = 0.0;

   last_time = now;
   last_val = val.f;
#else
   val.f = 0.0;
#endif

   return( val );
}



g_val_t
cpu_used_func( void )
{
   g_val_t val;


   val = cpu_physc_func();

   return( val );
}



g_val_t
disk_iops_func( void )
{
   g_val_t val;
   static perfstat_disk_total_t d1, d2;
   static double last_time = 0.0;
   static double last_val = 0.0;
   longlong_t diff = 0LL;
   double now, delta_t;
   struct timeval timeValue;
   struct timezone timeZone;


   gettimeofday( &timeValue, &timeZone );

   now = (double) (timeValue.tv_sec - boottime) + (timeValue.tv_usec / 1000000.0);

   if (perfstat_disk_total( NULL, &d2, sizeof( perfstat_disk_total_t ), 1 ) == -1)
      val.d = 0.0;
   else
   {
      delta_t = now - last_time;

      if ( delta_t > 0.0 )
      {
         diff = d2.xfers - d1.xfers;

         if (diff >= 0LL)
            val.d = diff / delta_t;
         else
            val.d = last_val;
      }
      else
         val.d = 0.0;

      d1 = d2;
   }

   last_time = now;
   last_val = val.d;

   return( val );
}



g_val_t
disk_read_func( void )
{
   g_val_t val;
   static perfstat_disk_total_t d1, d2;
   static double last_time = 0.0;
   static double last_val = 0.0;
   longlong_t diff = 0LL;
   double now, delta_t;
   struct timeval timeValue;
   struct timezone timeZone;


   gettimeofday( &timeValue, &timeZone );

   now = (double) (timeValue.tv_sec - boottime) + (timeValue.tv_usec / 1000000.0);
   if (perfstat_disk_total( NULL, &d2, sizeof( perfstat_disk_total_t ), 1 ) == -1)
      val.d = 0.0;
   else
   {
      delta_t = now - last_time;

      if ( delta_t > 0.0 )
      {
         diff = d2.rblks - d1.rblks;

         if (diff >= 0LL)
/* the result is returned in number of 512 byte blocks */
            val.d = (diff * 512.0) / delta_t;
         else
            val.d = last_val;
      }
      else
         val.d = 0.0;

      d1 = d2;
   }

   last_time = now;
   last_val = val.d;

   return( val );
}



g_val_t
disk_write_func( void )
{
   g_val_t val;
   static perfstat_disk_total_t d1, d2;
   static double last_time = 0.0;
   static double last_val = 0.0;
   longlong_t diff = 0LL;
   double now, delta_t;
   struct timeval timeValue;
   struct timezone timeZone;


   gettimeofday( &timeValue, &timeZone );

   now = (double) (timeValue.tv_sec - boottime) + (timeValue.tv_usec / 1000000.0);

   if (perfstat_disk_total( NULL, &d2, sizeof( perfstat_disk_total_t ), 1 ) == -1)
      val.d = 0.0;
   else
   {
      delta_t = now - last_time;

      if ( delta_t > 0.0 )
      {
         diff = d2.wblks - d1.wblks;

         if (diff >= 0LL)
/* the result is returned in number of 512 byte blocks */
            val.d = (diff * 512.0) / delta_t;
         else
            val.d = last_val;
      }
      else
         val.d = 0.0;

      d1 = d2;
   }

   last_time = now;
   last_val = val.d;

   return( val );
}



g_val_t
fwversion_func( void )
{
   FILE    *f;
   g_val_t  val;


   f = popen( "/usr/sbin/lsattr -El sys0 -a fwversion | /usr/bin/awk '{ print $2 }' 2>/dev/null", "r" );

   if (f == NULL)
   {
      strcpy( val.str, "popen 'lsattr -El sys0' failed" );
   }
   else
   {
      if ( fgets( val.str, MAX_G_STRING_SIZE, f ) != NULL )
      {
         val.str[MAX_G_STRING_SIZE - 1] = '\0';
         val.str[strlen( val.str ) - 1] = '\0';  /* truncate \n */
      }
      else
         strcpy( val.str, "Can't run AIX cmd 'lsattr'" );

      pclose( f );
   }

   return( val );
}



g_val_t
kernel64bit_func( void )
{
   g_val_t val;

   strcpy ( val.str, __KERNEL_64() ? "yes" : "no" );

   return( val );
}



g_val_t
lpar_func( void )
{
   g_val_t val;

#if defined(_AIX43)
   strcpy ( val.str, "no" );
#else
   strcpy ( val.str, __LPAR() ? "yes" : "no" );
#endif

   return( val );
}



g_val_t
lpar_name_func( void )
{
   g_val_t val;
#if defined(_AIX53) || defined(_AIX61) || defined(_AIX71) || defined(_AIX72)
   perfstat_partition_total_t p;


   if ( __LPAR() )
   {
      if (perfstat_partition_total( NULL, &p, sizeof( perfstat_partition_total_t ), 1) == -1)
         strcpy( val.str, "libperfstat returned an error" );
      else
         strcpy( val.str, p.name );
   }
   else
      strcpy( val.str, "No LPAR system" );
#else
   FILE *f;
   char  buf[64], *p;

#if defined(_AIX43)
   if ( 0 )
#else
   if ( __LPAR() )
#endif
   {
      f = popen( "/usr/bin/uname -L", "r" );

      if (f == NULL)
         strcpy( val.str, "popen of cmd 'uname -L' failed" );
      else
      {
         if ( fgets( buf, 64, f ) != NULL)
         {
            p = &buf[0];
            while (*p != ' ') p++;
            p++;

            strncpy( val.str, p, MAX_G_STRING_SIZE );
            val.str[MAX_G_STRING_SIZE - 1] = '\0';
            val.str[strlen( val.str ) - 1] = '\0';  /* truncate \n */
         }
         else
            strcpy( val.str, "Can't run 'uname -L'" );

         pclose( f );
      }
   }
   else
      strcpy( val.str, "No LPAR system" );
#endif

   return( val );
}



g_val_t
lpar_num_func( void )
{
   g_val_t val;
#if defined(_AIX53) || defined(_AIX61) || defined(_AIX71) || defined(_AIX72)
   perfstat_partition_total_t p;


   if (perfstat_partition_total( NULL, &p, sizeof( perfstat_partition_total_t ), 1) == -1)
      val.int32 = -1;
   else
      val.int32 = p.lpar_id;
#else
   FILE *f;
   char s[MAX_G_STRING_SIZE];

#if defined(_AIX43)
   if ( 0 )
#else
   if ( __LPAR() )
#endif
   {
      f = popen( "/usr/bin/uname -L 2>/dev/null", "r" );
      if (f == NULL)
         val.int32 = -1;
      else
      {
         if ( fgets( s, MAX_G_STRING_SIZE, f ) != NULL)
            val.int32 = atoi( s );
         else
            val.int32 = -1;

         pclose( f );
      }
   }
   else
      val.int32 = -1;
#endif

   return( val );
}



g_val_t
model_name_func( void )
{
   FILE    *f;
   g_val_t  val;


   f = popen( "/usr/bin/uname -M 2>/dev/null", "r" );

   if (f == NULL)
   {
      strcpy( val.str, "popen of cmd 'uname -M' failed" );
   }
   else
   {
      if ( fgets( val.str, MAX_G_STRING_SIZE, f ) != NULL )
      {
         val.str[MAX_G_STRING_SIZE - 1] = '\0';
         val.str[strlen( val.str ) - 1] = '\0';  /* truncate \n */
      }
      else
         strcpy( val.str, "Can't run AIX cmd 'uname'" );

      pclose( f );
   }

   return( val );
}



static g_val_t
oslevel_func_CALLED_ONCE( void )
{
   FILE    *f;
   g_val_t  val;


   if (isVIOserver)
   {
      f = popen( "/usr/ios/cli/ioscli ioslevel 2>/dev/null", "r" );

      if (f == NULL)
      {
         strcpy( val.str, "popen of cmd 'ioscli' failed" );
      }
      else
      {
         if ( fgets( val.str, MAX_G_STRING_SIZE, f ) != NULL )
         {
            val.str[MAX_G_STRING_SIZE - 1] = '\0';
            val.str[strlen( val.str ) - 1] = '\0';  /* truncate \n */
         }
         else
            strcpy( val.str, "Can't run AIX cmd 'ioscli'" );

         pclose( f );
      }
   }
   else
   {
      f = popen( "/usr/bin/oslevel -s 2>/dev/null", "r" );

      if (f == NULL)
      {
         strcpy( val.str, "popen of cmd 'oslevel -s' failed" );
      }
      else
      {
         if ( fgets( val.str, MAX_G_STRING_SIZE, f ) != NULL )
         {
            val.str[MAX_G_STRING_SIZE - 1] = '\0';
            val.str[strlen( val.str ) - 1] = '\0';  /* truncate \n */
         }
         else
            strcpy( val.str, "Can't run AIX cmd 'oslevel'" );

         if (! strncmp( val.str, "Usage: oslevel", 14 ))
         {
            pclose( f );

            f = popen( "/usr/bin/oslevel -r 2>/dev/null", "r" );
      
            if ( fgets( val.str, MAX_G_STRING_SIZE, f ) != NULL )
            {
               val.str[MAX_G_STRING_SIZE - 1] = '\0';
               val.str[strlen( val.str ) - 1] = '\0';  /* truncate \n */
            }
            else
               strcpy( val.str, "Can't run AIX cmd 'oslevel'" );
         }

         pclose( f );
      }
   }

   return( val );
}



g_val_t
oslevel_func( void )
{
   static g_val_t val;
   static int firstTime = 1;


   if (firstTime)
   {
      val = oslevel_func_CALLED_ONCE();
      firstTime = 0;
   }

   return( val );
}



g_val_t
serial_num_func( void )
{
   FILE    *f;
   g_val_t  val;


   f = popen( "/usr/bin/uname -u 2>/dev/null", "r" );

   if (f == NULL)
   {
      strcpy( val.str, "popen of cmd 'uname -u' failed" );
   }
   else
   {
      if ( fgets( val.str, MAX_G_STRING_SIZE, f ) != NULL )
      {
         val.str[MAX_G_STRING_SIZE - 1] = '\0';
         val.str[strlen( val.str ) - 1] = '\0';  /* truncate \n */
      }
      else
      {
         strcpy( val.str, "Can't run AIX cmd 'uname -u'" );
      }

      pclose( f );
   }

   return( val );
}



g_val_t
smt_func( void )
{
   g_val_t val;
#if defined(_AIX53) || defined(_AIX61) || defined(_AIX71) || defined(_AIX72)
   perfstat_partition_total_t p;
   FILE *f;
   char  buf[512];
   int   l;


   if (perfstat_partition_total( NULL, &p, sizeof( perfstat_partition_total_t), 1 ) == -1)
      strcpy( val.str, "libperfstat returned an error" );
   else
   {
      if (p.type.b.smt_capable)  /* system is SMT capable */
      {
         strcpy( val.str, "undefined" );

         f = popen( "/usr/bin/lparstat | /usr/bin/grep smt= | /usr/bin/awk '{ print $5 }' 2>/dev/null", "r" );
         if (f == NULL)
         {
            strcpy( val.str, "popen 'lparstat' failed" );
         }
         else
         {
            if (fgets( buf, 512, f ) != NULL)
            {
               buf[511] = '\0';  /* terminate string */
               l = strlen( buf );
               if (l > 1)
                  buf[l - 1] = '\0';  /* truncate \n */

               if (! strcmp( buf, "smt=Off" ) )
                  strcpy( val.str, "no (SMT=1)" );
               else if (! strcmp( buf, "smt=On" ) )
                  strcpy( val.str, "yes (SMT=2)" );
               else if (! strcmp( buf, "smt=4" ) )
                  strcpy( val.str, "yes (SMT=4)" );
               else if (! strcmp( buf, "smt=8" ) )
                  strcpy( val.str, "yes (SMT=8)" );
            }
            else
               strcpy( val.str, "Can't run AIX cmd 'lparstat'" );

            pclose( f );
         }
      }
      else
         strcpy( val.str, "No SMT-capable system" );
   }
#else
   strcpy( val.str, "No SMT-capable system" );
#endif

   return( val );
}



g_val_t
splpar_func( void )
{
   g_val_t val;
#if defined(_AIX53) || defined(_AIX61) || defined(_AIX71) || defined(_AIX72)
   perfstat_partition_total_t p;


   if (perfstat_partition_total( NULL, &p, sizeof( perfstat_partition_total_t), 1) == -1)
      strcpy( val.str, "libperfstat returned an error" );
   else
      strcpy( val.str, p.type.b.shared_enabled ? "yes" : "no" );
#else
   strcpy( val.str, "No SPLPAR-capable system" );
#endif

   return( val );
}



g_val_t
weight_func( void )
{
   g_val_t val;
#if defined(_AIX53) || defined(_AIX61) || defined(_AIX71) || defined(_AIX72)
   perfstat_partition_total_t p;


   if (perfstat_partition_total( NULL, &p, sizeof( perfstat_partition_total_t), 1 ) == -1)
      val.int32 = -1;
   else
      if ( p.type.b.shared_enabled )
         val.int32 = p.var_proc_capacity_weight;
      else
         val.int32 = -1;
#else
   val.int32 = -1;
#endif

   return( val );
}



g_val_t
kvm_guest_func( void )
{
   g_val_t val;


   strcpy( val.str, "no" );

   return( val );
}



g_val_t
cpu_type_func( void )
{
   return( machine_type_func() );
}



static time_t
boottime_func_CALLED_ONCE( void )
{
   time_t boottime;
   struct utmp buf;
   FILE *utmp;


   utmp = fopen( UTMP_FILE, "r" );

   if (utmp == NULL)
   {
      /* Can't open utmp, use current time as boottime */
      boottime = time( NULL );
   }
   else
   {
      while (fread( (char *) &buf, sizeof( buf ), 1, utmp ) == 1)
      {
         if (buf.ut_type == BOOT_TIME)
         {
            boottime = buf.ut_time;
            break;
        }
      }

      fclose( utmp );
   }

   return( boottime );
}



/*
 * Declare ourselves so the configuration routines can find and know us.
 * We'll fill it in at the end of the module.
 */
extern mmodule ibmpower_module;


static int ibmpower_metric_init ( apr_pool_t *p )
{
   int i;
   FILE *f;
   g_val_t val;


   for (i = 0;  ibmpower_module.metrics_info[i].name != NULL;  i++)
   {
      /* Initialize the metadata storage for each of the metrics and then
       *  store one or more key/value pairs.  The define MGROUPS defines
       *  the key for the grouping attribute. */
      MMETRIC_INIT_METADATA( &(ibmpower_module.metrics_info[i]), p );
      MMETRIC_ADD_METADATA( &(ibmpower_module.metrics_info[i]), MGROUP, "ibmpower" );
   }


/* find out if we are running on a VIO server */

   f = fopen( "/usr/ios/cli/ioscli", "r" );

   if (f)
   {
      isVIOserver = 1;
      fclose( f );
   }
   else
      isVIOserver = 0;


/* initialize the routines which require a time interval */

   boottime = boottime_func_CALLED_ONCE();
   val = cpu_pool_idle_func();
   val = cpu_used_func();
   val = disk_iops_func();
   val = disk_read_func();
   val = disk_write_func();

   return( 0 );
}



static void ibmpower_metric_cleanup ( void )
{
}



static g_val_t ibmpower_metric_handler ( int metric_index )
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
   {0, "oslevel",          180, GANGLIA_VALUE_STRING,       "",     "both", "%s",   UDP_HEADER_SIZE+64, "Exact AIX version string"},
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

