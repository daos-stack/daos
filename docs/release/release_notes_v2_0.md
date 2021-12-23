# DAOS Version 2.0 Release Notes

We are pleased to announce the release of DAOS version 2.0.


!!!note
	The DAOS version 2.0 java/hadoop DAOS connector has been updated to use
	Log4j version 2.16 and may not include the latest functional and security
	updates. DAOS 2.0.1 is targeted to be released in January 2022 and will
	include additional functional and/or security updates. Customers should
	update to the latest version as it becomes available.



## General Support

In this release we have added the following changes to the DAOS support matrix:

- Starting with DAOS Version 2.0, Commercial Level-3 Support for DAOS is
 available
- Added support for 3rd gen Intel(r) Xeon(r) scalable processors and Intel
 Optane Persistent memory 200 series
- CentOS Linux 8 and openSUSE Leap 15.3  support is added

For a complete list of supported hardware and software, refer to the
 [Support Matrix](https://docs.daos.io/v2.0/release/support_matrix_v2_0/).






## Key features and improvements

### Erasure code

With the 2.0 release, DAOS provides the option of Reed Solomon based EC for data
 protection, supporting EC data recovery for storage target failure, and data
 migration while extending the storage pool. Main sub-features of DAOS EC
 include:



- Reed-Solomon based EC support for I/O.

- Versioned data aggregation for EC protected object.

- Data recovery for EC protected object.

### Telemetry and monitoring

DAOS maintains metrics and statistics for each storage engine while the engines
 are running to provide insight into DAOS health and performance and
 troubleshooting. Integration with System RAS enables proactive notification of
 critical DAOS/Storage events. This data can be aggregated over all nodes in
 the system by external tools (such as a time-series database) to present
 overall bandwidth and other statistics. The information provided includes
 bytes read and written to the engine’s storage, I/O latency, I/O operations,
 error events, and internal state.


### Pool and container labels

To improve ease of use, DAOS 2.0 introduces labels (in addition to UUID) as an
option to identify and reference pools and containers.

### Improved usability and management capabilities

DAOS 2.0 has added a number of usability and management improvements, such as
improving command structures for consistency and automated client resource
management that allows DAOS to be resilient even if clients are not.



### Increased flexibility in object layout

There are now two ways to specify the object class:

- by using a pre-defined object class (e.g., OC_S1) as before. This will be
converted
into a OR_{RP|RS} redundancy schema + a number of groups.
- by daos_obj_generate_oid() which is now not limited any longer to the
predefined list, providing a greater flexibility of options.

### mpifileutils integration

Tools for parallel data set copy are located within mpiFileUtils.  mpiFileUtils
 provides an MPI-based suite of tools to handle large datasets.  A DAOS backend
 was written to support tools like dcp and dsync.








## Known Issues and limitations

- Validation of CentOS 8.5 indicates an integration issue with
MLNX_OFED_LINUX-5.5-1.0.3.2 and 5.4-3.1.0.0

	It is recommended to not use these versions of mofed on CentOS 8.5 until
	this issue is resolved.

- CPU utilization

	Some users have reported instances of high CPU utilization on DAOS servers
	when the system is at rest. This will be investigated and resolved in a
	future release.

- Add support for co-located (targets) shards in placement [DAOS-5499]
(https://daosio.atlassian.net/browse/DAOS-5499)

- Proper handling of RPCs to self when there is only 1 server remaining
 (rebuild when 1 of 2 servers killed) [DAOS-6085]
 (https://daosio.atlassian.net/browse/DAOS-6085)


- DASO fs copy does not support symlinks [DAOS-9254]
(https://daosio.atlassian.net/browse/DAOS-9254)

- No OPA/PSM2 support

- der_nospace under certain conditions [DAOS-8400]
(https://daosio.atlassian.net/browse/DAOS-8400)


- The daos_server.yml file has security enabled by default [DAOS-8114]
(https://daosio.atlassian.net/browse/DAOS-8114)

	If a user has not deployed certificates, and uses the default yaml, the DAOS
	server will crash. Users should deploy certificates if secure mode is
	required.

A complete list of known issues in v2.0 can be found [**HERE**]
(https://daosio.atlassian.net/issues/?jql=project%20in%20(DAOS%2C%20CART)%20AND%20type%20%3D%20bug%20AND%20statuscategory%20!%3D%20done%20AND%20affectedVersion%20!%3D%20%222.1%20Community%20Release%22%20AND%20%22Bug%20Source%22%20!%3D%20%22non-product%20bug%22%20ORDER%20BY%20priority%20DESC).



## Bug fixes

The DAOS 2.0 release includes fixes for 477 defects, including:

- DAOS 2.0 has moved to Libfabric version 1.14 and Mercury 2.1, which includes a
 number of stability and scalability fixes.
- No longer shipping DAOS tests that caused dependency conflicts with MOFED.
- DAOS 2.0 fixes a number of bugs dealing with pool and container destroy that
 could result in unremovable pools/containers.
- The interception library was not correctly intercepting mkstemp(). This has
 been resolved in the 2.0 release. [DAOS--8822]
 (https://daosio.atlassian.net/browse/DAOS-8822
- DAOS v2.0 resolves a number of memory leak issues in prior test builds.

A complete list of bugs resolved in v2.0 can be found [**HERE**](https://daosio.atlassian.net/issues/?jql=project%20in%20(DAOS%2C%20CART)%20AND%20type%20%3D%20bug%20AND%20statuscategory%20%3D%20done%20AND%20resolution%20in%20(fixed%2C%20Fixed-Verified%2C%20Done)%20AND%20fixversion%20%3D%20%222.0%20Community%20Release%22%20AND%20%22Bug%20Source%22%20!%3D%20%22non-product%20bug%22%20ORDER%20BY%20priority%20DESC).








## Additional resources

Visit the [online documentation](https://daos-stack.github.io/) for more
information. All DAOS project source code is maintained in the
[https://github.com/daos-stack/daos](https://github.com/daos-stack/daos)
 repository.
Please visit this [link](https://github.com/daos-stack/daos/blob/master/LICENSE)
for more information on the licenses.

Refer to the [Software Installation](https://docs.daos.io/v2.0/admin/installation/)
section of the [DAOS Administration Guide]
(https://daos-stack.github.io/admin/hardware/)
for installation details.
