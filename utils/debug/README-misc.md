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


###

Get information such as the type of executable could be get thanks to the `readelf` command
```
[1097] > readelf -h daos_engine
ELF Header:
  Magic:   7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00
  Class:                             ELF64
  Data:                              2's complement, little endian
  Version:                           1 (current)
  OS/ABI:                            UNIX - System V
  ABI Version:                       0
  Type:                              DYN (Shared object file)
  Machine:                           Advanced Micro Devices X86-64
  Version:                           0x1
  Entry point address:               0xff100
  Start of program headers:          64 (bytes into file)
  Start of section headers:          6877488 (bytes into file)
  Flags:                             0x0
  Size of this header:               64 (bytes)
  Size of program headers:           56 (bytes)
  Number of program headers:         10
  Size of section headers:           64 (bytes)
  Number of section headers:         32
  Section header string table index: 31
```
