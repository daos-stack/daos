# Run DAOS Autotest

DAOS autotest tests the proper setup of the DAOS configuration, which is used to test the setup.
DAOS autotest performs various activities like connecting to a pool, creating and opening
a container, then reading and writing to the container.

First of all, a pool must be created with dmg to run autotest:

```sh
$ dmg pool create --size=50G autotest_pool

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
```

Then run autotest via the daos utility:

```sh
$ daos pool autotest autotest_pool

# Sample output
Step Operation                 Status Time(sec) Comment
  0  Initializing DAOS          PASS    0.000  
  1  Connecting to pool         PASS    0.026  
  2  Creating containers        PASS    0.007  
  3  Opening container          PASS    0.032  
 10  Generating 1M S1 layouts   PASS    1.785   559.56K IO/sec
 11  Generating 10K SX layouts  PASS    0.019   524.53K IO/sec
 20  Inserting 128B values      PASS   30.008    72.27K IO/sec
 21  Reading 128B values back   PASS   29.131    74.49K IO/sec
 23  Punching object            PASS    0.096  
 24  Inserting 4KB values       PASS   30.006    53.81K IO/sec
 25  Reading 4KB values back    PASS   37.766    42.88K IO/sec
 27  Punching object            PASS    0.091  
 28  Inserting 1MB values       PASS   30.011     1.63K IO/sec
 29  Reading 1MB values back    PASS   23.982     2.02K IO/sec
 31  Punching object            PASS    0.109  
 40  Inserting into RF1 cont    PASS   30.006    57.95K IO/sec
 41  Reading RF1 values back    PASS   24.244    74.56K IO/sec
 42  Inserting into RF2 cont    PASS   30.005    54.09K IO/sec
 43  Reading RF2 values back    PASS   23.267    74.64K IO/sec
 96  Closing containers         PASS    0.009  
 97  Destroying containers      PASS    0.005  
 98  Disconnecting from pool    PASS    0.001  
 99  Tearing down DAOS          PASS    0.000  

All steps passed.
```
