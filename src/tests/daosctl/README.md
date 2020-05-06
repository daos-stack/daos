# daosctl
At present this is utility for sending commands to test DAOS. There are
hooks and placeholders etc. that would allow this tool to be extended in the
future to be a general purpose management too.  The test commands
are in general, not useful for any purpose beyond testing and may be
destructive and/or have undesirable side affects.

General Purpose Commands:

	* create-pool -- create a DAOS pool
	* destroy-pool -- destroy a DAOS pool
	* evict-pool -- logout clients that have connected to a pool
	* help -- more of placeholder than anything truly helpful

Test Commands:
	* connect-pool -- test connection API arguments
	* test-create-pool -- test create pool arguments
	* test-connect-pool -- test create/connect/destroy scenarios
	* test-evict-pool -- test eviction parameters/scenarios
	* test-query-pool -- compares creation info against query info

Build
=====
Integrated into the DAOS SCons build system.
