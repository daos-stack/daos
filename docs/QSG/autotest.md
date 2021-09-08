# Run DAO Autotest

DAOS autotest tests the proper setup of the DAOS configuration, which is used to test the setup. DAOS autotest
performs various activities like connecting to a pool, creating and opening
a container, then reading and writing to the container.

	# create pool
	dmg pool create --size=50G

	# sample output
	Creating DAOS pool with automatic storage allocation: 50 GB NVMe + 6.00% SCM
	Pool created with 6.00% SCM/NVMe ratio
	---------------------------------------
	  UUID          : 6af46954-f704-45f0-8b80-115ef855a065
	  Service Ranks : [1-3]
	  Storage Ranks : [0-3]
	  Total Size    : 53 GB
	  SCM           : 3.0 GB (750 MB / rank)
	  NVMe          : 50 GB (12 GB / rank)

	# assign pool uuid to a variable
	export DAOS_POOL=<pool uuid>

	# run daos autotest
	daos pool autotest --pool $DAOS_POOL

	# Sample output
	Step Operation               Status Time(sec) Comment
	  0  Initializing DAOS          OK      0.000
	  1  Connecting to pool         OK      0.070
	  2  Creating container         OK      0.000  uuid =
	  3  Opening container          OK      0.050
	 10  Generating 1M S1 layouts   OK      4.620
	 11  Generating 10K SX layouts  OK      0.140
	 20  Inserting 1M 128B values   OK     75.130
	 21  Reading 128B values back   OK     71.540
	 24  Inserting 1M 4KB values    OK    109.190
	 25  Reading 4KB values back    OK    103.620
	 28  Inserting 100K 1MB values  OK    413.730
	 29  Reading 1MB values back    OK    461.220
	 96  Closing container          OK      0.040
	 97  Destroying container       OK      0.070
	 98  Disconnecting from pool    OK      0.000
	 99  Tearing down DAOS          OK      0.000

## Clean Up

Remove one of the copy created using datamover

	rm -rf /tmp/daos_dfuse/daos_container_copy

Remove dfuse mountpoint:

	# unmount dfuse
	pdsh -w $CLIENT_NODES 'fusermount3 -uz /tmp/daos_dfuse'

	# remove mount dir
	pdsh -w $CLIENT_NODES rm -rf /tmp/daos_dfuse



List containers to be destroyed:

	# list containers
	daos pool list-containers --pool $DAOS_POOL  # sample output

	# sample output
	cd46cf6e-f886-4682-8077-e3cbcd09b43a
	caf0135c-def8-45a5-bac3-d0b969e67c8b

Destroy Containers:

	# destroy container1
	daos container destroy --pool $DAOS_POOL --cont $DAOS_CONT

	# destroy container2
	daos container destroy --pool $DAOS_POOL --cont $DAOS_CONT2



List Pools to be destroyed:

	# list pool
	dmg pool list

	# sample output
	Pool UUID                            Svc Replicas
	---------                            ------------
	b22220ea-740d-46bc-84ad-35ed3a28aa31 [1-3]



Destroy Pool:

	# destroy pool
	dmg pool destroy --pool $DAOS_POOL


Stop Agents:

	# stop agents
	pdsh -S -w $CLIENT_NODES "sudo systemctl stop daos_agent"

Stop Servers:

\# stop servers

pdsh -S -w \$SERVER_NODES \"sudo systemctl stop daos_server\"


