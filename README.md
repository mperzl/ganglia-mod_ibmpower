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

Metric:	capped

Return type:
* `GANGLIA_VALUE_STRING`

Notion:
* This metric either returns “yes” if the system is a POWER5 Shared Processor LPAR which is running in capped mode or “no” otherwise. 

