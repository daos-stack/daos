# UCX Fabric Support

DAOS 2.4 includes
[UCX](https://www.openucx.org/) support for clusters using InfiniBand,
as an alternative to the default
[libfabric](https://ofiwg.github.io/libfabric/) network stack.

!!! note The network provider is an immutable property of a DAOS system.
         Changing the network provider to UCX requires that the DAOS storage
         is reformatted. 

To enable DAOS UCX support on InfiniBand fabrics,
the following steps are needed:

*  A supported version of MLNX\_OFED must be installed _before_
   DAOS is installed. This is the same for libfabric and for UCX:
   DAOS only supports the NVIDIA-provided MLNX\_OFED stack,
   not the inbox drivers.
   Refer to the [DAOS Support Matrix](../release/support_matrix)
   for information about supported MLNX\_OFED releases.

*  The `mercury-ucx` RPM package needs to be **manually** selected for
   installation:

   - The base `mercury` RPM package ships with the libfabric plugin.
     This RPM will be installed by default and is a dependency of the
     `mercury-ucx` RPM.

   - The additional `mercury-ucx` RPM is also provided. This RPM contains
     the UCX plugin that is required for enabling UCX support.
     This RPM **must** be used in
     InfiniBand environments when the intention is to use
     UCX.
     Attempts to install this RPM in non-Infiniband environments
     will fail, because it has a dependency on UCX packages.

*  At DAOS **installation** time, to enable UCX support the
   `mercury-ucx` RPM package must be explicitly listed.
   For example, using the `yum`/`dnf` package manager on EL8:

```bash
      # on DAOS_ADMIN nodes:
      yum install mercury-ucx daos-admin

      # on DAOS_SERVER nodes:
      yum install mercury-ucx daos-server

      # on DAOS_CLIENT nodes:
      yum install mercury-ucx daos-client
```

After UCX support has been enabled by installing the `mercury-ucx`
package, the network provider must be changed in the DAOS server's
configuration file (`/etc/daos/daos_server.yml`).
A sample YML file is available on
[github](https://github.com/daos-stack/daos/blob/release/2.4/utils/config/examples/daos_server_ucx.yml).
The recommended setting for UCX is `provider: ucx+dc_x`.
