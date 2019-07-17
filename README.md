# ganglia-mod_ibmpower
Ganglia Monitor daemon module ibmpower

This gmond module provides Shared Processor LPAR metrics (IBM POWER5 and higher) on AIX and Linux on Power.

License: BSD 3-Clause "New" or "Revised" License

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

As most of these metrics are only useful for AIX 5L v5.3 or higher running in a Shared Processor LPAR, some "reasonable" values must be returned if not running in such a scenario.

----

Metric: **`capped`**

**Return type:** `GANGLIA_VALUE_STRING`

* This metric either returns “**yes**” if the system is a POWER Shared Processor LPAR which is running in capped mode or “**no**” otherwise. 

----

Metric:	**`cpu_entitlement`**

**Return type:** `GANGLIA_VALUE_FLOAT`

* This metric returns the Capacity Entitlement of the system in units of physical cores.
* If we are running on AIX 5L v5.3 or later a distinction must be made whether this is a Shared Processor LPAR or not as otherwise the number of online CPUs is returned.
* On AIX versions before V5.3 the number of available CPUs is returned.
* If libperfstat returns an error code a value of 0.0 is returned.

----

Metric:	**`cpu_in_lpar`**

**Return type:** `GANGLIA_VALUE_INT`

* This metric returns the number of CPUs the OS sees in the system.
* In a POWER Shared Processor LPAR this returns the number of logical CPUs.
* If we are running on AIX 5L v5.3 or later the number of online CPUs is returned.
* On AIX versions before v5.3 the number of configured CPUs is returned.
* If libperfstat returns an error code a value of -1 is returned.

----

Metric:	**`cpu_in_machine`**

**Return type:** `GANGLIA_VALUE_INT`

* This metric returns the number of physical CPUs in the whole system.
* If we are running on AIX 5L v5.3 or later the number of online physical CPUs is returned.
* On AIX versions before v5.3 the number of CPUs is returned.
* If libperfstat returns an error code a value of -1 is returned.

----

Metric:	**`cpu_in_pool`**

**Return type:** `GANGLIA_VALUE_INT`

* This metric returns the number of physical CPUs in the Shared Processor Pool.
* On AIX versions before v5.3 the number of CPUs is returned.
* If libperfstat returns an error code a value of -1 is returned.

----

Metric:	**`cpu_pool_idle`**

**Return type:** `GANGLIA_VALUE_FLOAT`

* This metric returns in fractional numbers of physical CPUs how much the Shared Processor Pool is idle.
* For example, if 7 physical CPUs are in the Shared Processor Pool, a value of 4.69 might be returned meaning that only an amount of (7 – 4.69) = 2.31 physical CPUs were used since the last time this metric was measured.
* For good numerical results the time stamps are measured in µ-seconds.
* The Shared Processor Pool idle time is returned in nano-seconds from libperfstat.
* On AIX versions before v5.3 no Shared Processor Pool exists and thus a value of 0.0 is returned.
* If libperfstat returns an error code a value of 0.0 is returned.

----

Metric:	**`cpu_used`**

**Return type:** `GANGLIA_VALUE_FLOAT`

* This metric returns in fractional numbers of physical CPUs how much compute resources this shared processor has used since the last time this metric was measured.
* For example, if the LPAR is running in uncapped mode and has a Capacity Entitlement of 0.2 physical CPUs and a value of 0.5 is measured then this LPAR has used 2.5 × its entitled capacity since the last time this metric was measured (i.e., basically using 250% of its entitled CPU resources for this measured time interval).
* For good numerical results the time stamps are measured in µ-seconds.
* The CPU used metric value is returned in nano-seconds from libperfstat.
* If we are running on AIX 5L v5.3 or later a distinction must be made whether this is a Shared Processor LPAR or not as otherwise the number of online CPUs is returned.
* On AIX versions before v5.3 the number of configured CPUs is returned.
* If libperfstat returns an error code a value of 0.0 is returned.

----

Metric:	**`disk_read`**

**Return type:** `GANGLIA_VALUE_FLOAT`

* This metric returns in units of kB the total read I/O of the system.
* For good numerical results the time stamps are measured in µ-seconds.
* The total disk read I/O of the system is returned in 512 byte blocks from libperfstat.
* If libperfstat returns an error code a value of 0.0 is returned.

----

Metric:	**`disk_write`**

**Return type:** `GANGLIA_VALUE_FLOAT`

* This metric returns in units of kB the total write I/O of the system.
* For good numerical results the time stamps are measured in µ-seconds.
* The total disk write I/O of the system is returned in 512 byte blocks from libperfstat.
* If libperfstat returns an error code a value of 0.0 is returned.

----

Metric:	**`kernel64bit`**

**Return type:** `GANGLIA_VALUE_STRING`

* This metric either returns “**yes**” if the running AIX kernel is a 64-bit kernel or “**no**” otherwise.

----

Metric:	**`lpar`**

**Return type:** `GANGLIA_VALUE_STRING`

* This metric either returns “**yes**” if the system is a LPAR or “**no**” otherwise.

----

Metric:	**`lpar_name`**

**Return type:** `GANGLIA_VALUE_STRING`

* This metric returns the name of the LPAR as defined on the Hardware Management Console (HMC) or some reasonable message otherwise.
* If we are running on AIX 5L v5.3 or later libperfstat can be used to obtain that information.
* On AIX versions before v5.3 libperfstat doesn't contain that information and therefore this must be obtained via the `uname` command.
* If libperfstat or `uname` return an error code an appropriate error message is returned.

----

Metric:	**`lpar_num`**

**Return type:** `GANGLIA_VALUE_INT`

* This metric returns the partition ID of the LPAR as defined on the Hardware Management Console (HMC) or some reasonable message otherwise.
* If we are running on AIX 5L v5.3 or later libperfstat can be used to obtain that information.
* On AIX versions before v5.3 ulibperfstat does not contain that information and therefore it must be obtained via the `uname` command.
* If libperfstat or `uname` return an error code a value of -1 is returned.

----

Metric:	**`oslevel`**

**Return type:** `GANGLIA_VALUE_STRING`

* This metric returns the version string as provided by the AIX command `oslevel`.
* Since AIX 5L v5.3 Technology Level 04 the oslevel command has an additional switch “`-s`”. First, we try to run `oslevel -s` and if that fails then we try to run `oslevel -r`.
* This metric is retrieved only once and then “cached” for subsequential calls.
* If `oslevel` return an error code an appropriate error message is returned.

----

Metric:	**`serial_num`**

**Return type:** `GANGLIA_VALUE_STRING`

* This metric returns the serial number of the system as provided by the AIX command `uname`.
* This metric is retrieved only once and then “cached” for subsequential calls.
* If `uname` return an error code an appropriate error message is returned.

----

Metric:	**`smt`**

Return type:** `GANGLIA_VALUE_STRING`

* This metric either returns “**yes**” if SMT is enabled or “**no**” otherwise.
* If libperfstat returns an error code an appropriate error message is returned.

----

Metric:	**`splpar`**

Return type:** `GANGLIA_VALUE_STRING`

* This metric either returns “**yes**” if the system is running in a shared processor LPAR or “**no**” otherwise. 
* If libperfstat returns an error code an appropriate error message is returned.

----

Metric:	**`weight`**

**Return type:** `GANGLIA_VALUE_INT`

* This metric returns the weight of the LPAR running in uncapped mode.
* On AIX versions before v5.3 a value of -1 is returned.
* If libperfstat returns an error code a value of -1 is returned.
