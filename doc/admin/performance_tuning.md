DAOS Performance Tuning
=======================

This section will be expanded in a future revision.

Network Performance
-------------------

Similar to the Lustre Network stack, the DAOS CART layer has the ability
to validate and benchmark network communications in the same context as
an application and using the same networks/tuning options as regular
DAOS. The CART selftest can run against the DAOS servers in a production
environment in a non-destructive manner. CART selftest supports
different message sizes, bulk transfers, multiple targets and the
following test scenarios:

-   **Selftest client to servers** where selftest issues RPCs directly
    to a list of servers

-   **Cross-servers** where selftest sends instructions to the different
    servers that will issue cross-server RPCs. This model supports a
    many to many communication model.

Instructions on how to run CART selftest will be provided in the next
revision of this document.

Benchmarking DAOS
-----------------

DAOS can be benchmarked with both IOR and mdtest through the following
backends:

-   native MPI-IO plugin combined with the ROMIO DAOS ADIO driver

-   native HDF5 plugin combined with the HDF5 DAOS connector (under
    development)

-   native POSIX plugin over dfuse and interception library (under
    development)

-   a custom DFS plugin integrating mdtest & IOR directly with libfs
    without requiring FUSE or an interception library

-   a custom DAOS plugin integrating IOR directly with the native DAOS
    array API.


