*SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
Copyright (c) 2022-2023 Hewlett Packard Enterprise Development LP*

# Multinode Framework

The multinode_frmwk provides a framework for writing multinode test applications
under a Workload Manager (WLM).

The framework itself is controlled by a number of environment variables provided
by the WLM, or the user environment:

- **PMI_SIZE** is supplied by the WLM, and indicates the total number of nodes in the
job.

- **PMI_RANK** is supplied by the WLM, and indicates the rank of this instance of the
application.

- **PMI_SHARED_SECRET** is supplied by the WLM, and is a "magic number" (a nsec
timestamp) that is guaranteed to be common to all instances of the application,
and unique to each job.

- **PMI_NUM_HSNS** is supplied by the user environment, and defaults to 1 if not
specified. It can have a value from 1 to 4, and indicates the number of NICs
(per node) to bring into play.

- **PMI_HOME** is supplied by the user environment, and defaults to $HOME if not
specified. This indicates the file system directory used for the file system
Allgather operation, and must be readable and writable.

# APP: test_frmwk

The **test_frmwk** application is a basic sanity test for the framework itself.

$ srun -Nn ./test_frmwk [args...]

# APP: test_zbcoll

The **test_zbcoll** application is a full regression suite for the zbcoll
implementation, which provides a high-performance zero-buffer implementation of
Barrier, Broadcast, and IOR Reduce used in the process of bootstrapping the
collective join operation.

$ srun -Nn ./test_zbcoll [args...]

# APP: test_coll

The **test_coll** application is a full regression suite for the accelerated
collectives. It requires a multicast configuration service, which presents
itself as a REST API.

$ srun -Nn ./test_coll [args...]

## Simulated Multicast ##

A *simulated* multicast configuration service is provided in the multinode
subdirectory. It uses FLASK (Python), and returns a small number of specifically
invalid multicast addresses that are interpreted as a request for a UNICAST
implementation of collectives. This implementation is not performant and should
not be used in production -- it implements the broadcast phase of the
accelerated collective as a series of point-to-point sends from the HWRoot to
each leaf node, and as there is no multicast in-tree reduction, the HWRoot
becomes a target of an incast from all the leaf transmissions. This can be used,
however, to fully test the software paths and behaviors on small collective
groups, without any involvement from the fabric manager software.

The FLASK simulation is typically started in a window on the WLM job-launch node
as follows:

$ ./flask_fmgrsrv.py --host *ipaddress* --port *port*

The *ipaddress* can be obtained on the host where it is run using:

$ hostname -I | awk '{print $1}'

The *port* can be any valid, unused port. A value of 5000 typically works.

A number of environment variables control the libfabric collective behavior:

- **FI_CXI_COLL_JOB_ID** is an identifier unique to each job.

- **FI_CXI_COLL_JOB_STEP_ID** is an identifier unique to each job-step.

- **FI_CXI_COLL_MCAST_TOKEN** is a security token used to authenticate the
application to the fabric manager when using the REST API.

- **FI_CXI_HWCOLL_ADDRS_PER_JOB** is the maximum number of multicast addresses
  available to this job.

- **FI_CXI_HWCOLL_MIN_NODES** is the minimum number of endpoints required to support accelerated collectives.

- **FI_CXI_COLL_FABRIC_MGR_URL** is the URL for the fabric manager REST API.

- **FI_CXI_COLL_RETRY_USEC** is the time spent waiting for reduction
  completion before performing a retry.

- **FI_CXI_COLL_TIMEOUT_USEC** is the length of time hardware reduction engines
  will be reserved before timing out and delivering a partial result.

- **FI_CXI_COLL_USE_DMA_PUT** (experimental) uses Cassini DMA to initiate sends
for reduction packets.

The framework will set all of the above environment variables to usable
defaults, if they are not already specified in the user environment, with the
exception of **FI_CXI_COLL_FABRIC_MGR_URL**, which must be explicitly defined in
the user environment.

$ export FI_CXI_COLL_FABRIC_MGR_URL='http://*ipaddress*:*port*'

The simulated FLASK service can be tested using:

$ curl $FI_CXI_COLL_FABRIC_MGR_URL

This should return a JSON object containing help text strings.

**NOTE**: The simulated service uses http, not https.

## Production Multicast ##

Full-scale (performant) test_coll runs can be performed by specifying the real
fabric manager REST API URL.

This will require that the WLM export a valid **FI_CXI_COLL_MCAST_TOKEN** in the
job environment after acquiring the token for the job from the fabric manager.
This is an opaque session token that persists for the duration of the job.

**NOTE**: The real service uses https, not http, and is a trusted service.
