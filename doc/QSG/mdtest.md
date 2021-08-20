# Run mdtest

Create a substantial directory structure.

Use mdtest to create 30K files:

	mpirun -hostfile /path/to/hostfile_clients -np 10 mdtest -a POSIX -z 0 -F -C -i 1 -n 3334 -e 4096 -d /tmp/daos_dfuse/ -w 4096

	-- started at 04/29/2021 23:28:11 --

	mdtest-3.4.0+dev was launched with 10 total task(s) on 3 node(s)
	Command line used: mdtest '-a' 'POSIX' '-z' '0' '-F' '-C' '-i' '1' '-n' '3334' '-e' '4096' '-d' '/tmp/daos_dfuse/' '-w' '4096'
	Path: /tmp/daos_dfuse
	FS: 36.5 GiB   Used FS: 18.8%   Inodes: 2.3 Mi   Used Inodes: 5.9%

	Nodemap: 1001001001
	10 tasks, 33340 files

	SUMMARY rate: (of 1 iterations)
	   Operation                      Max            Min           Mean        Std Dev
	   ---------                      ---            ---           ----        -------
	   File creation             :       2943.697       2943.674       2943.686          0.006
	   File stat                 :          0.000          0.000          0.000          0.000
	   File read                 :          0.000          0.000          0.000          0.000
	   File removal              :          0.000          0.000          0.000          0.000
	   Tree creation             :       1079.858       1079.858       1079.858          0.000
	   Tree removal              :          0.000          0.000          0.000          0.000
	-- finished at 04/29/2021 23:28:22 --


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

	# stop servers
	pdsh -S -w $SERVER_NODES "sudo systemctl stop daos_server"

