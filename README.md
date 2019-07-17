# ganglia-mod_ibmpower
Ganglia Monitor daemon module ibmpower

This gmond module provides Shared Processor LPAR metrics (IBM Power5 and higher) on AIX and Linux on Power.

The following additional metrics are defined for AIX and Linux on Power:
* `capped`
* `cpu_entitlement`
* `cpu_in_lpar`
* `cpu_in_machine`
* `cpu_in_pool`
* `cpu_pool_idle`
* `cpu_used`
* `disk_read`
* `disk_write`
* `kernel64bit`
* `lpar`
* `lpar_name`
* `lpar_num`
* `oslevel`
* `serial_num`
* `smt`
* `splpar`
* `weight`

Despite the fact that most of these metrics are mostly only useful for AIX 5L v5.3 running in a Shared Processor LPAR, some "reasonable" values must be returned if not running in that scenario.

----

Metric: **capped**

**Return type:** `GANGLIA_VALUE_STRING`

* This functions either returns “**yes**” if the system is a POWER5 Shared Processor LPAR which is running in capped mode or “**no**” otherwise. 

----

Metric:	**entitlement**

**Return type:** `GANGLIA_VALUE_FLOAT`

* This function returns the Capacity Entitlement of the system in units of physical cores.
* If we are running on AIX 5L v5.3 or later a distinction must be made whether this is a Shared Processor LPAR or not as otherwise the number of online CPUs is returned.
* On AIX versions before V5.3 the number of available CPUs is returned.
* If libperfstat returns an error code a value of 0.0 is returned.

----
Metric:	**cpu_in_lpar**

**Return type:** `GANGLIA_VALUE_INT`

* This metric returns the number of CPUs the OS sees in the system.
* In a POWER5 Shared Processor LPAR this returns the number of logical CPUs.
* If we are running on AIX 5L v5.3 or later the number of online CPUs is returned.
* On AIX versions before v5.3 the number of configured CPUs is returned.
* If libperfstat returns an error code a value of -1 is returned.


