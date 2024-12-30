### Hardware Topology

to display the topology of a node the following command could be used:
```
lstopo-no-graphics --of ascii
```
This command is part of the hwloc package.


### Rebuild Status

rebuild status could be found in the following logs
```
12/19-10:41:39.80 m02r01s03dao DAOS[17945/0/1439] rebuild INFO src/rebuild/srv.c:2378 rebuild_tgt_status_check_ult() b60b0029 ver 325 gen 0 obj 0 rec 65536 size 65536 scan done 1 pull done 0 scan gl done 0 gl done 0 status 0 abort no
```
More details with the ticket DAOS-16902


### Logs Analysis

Logs analysis of the engine could be done thanks to the tool `src/tests/ftest/cart/util/daos_sys_logscan.py`.
