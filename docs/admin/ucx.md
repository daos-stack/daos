# UCX Fabric Support

For clusters using InfiniBand, DAOS supports [UCX](https://www.openucx.org/)
as an alternative to the default
[libfabric](https://ofiwg.github.io/libfabric/) network stack.
The UCX provider is fully supported since DAOS 2.4.

To enable DAOS UCX support on InfiniBand fabrics,
the following steps are needed:

*  A supported version of DOCA-OFED must be installed _before_
   DAOS is installed. The same applies to both libfabric and UCX:
   DAOS only supports the NVIDIA-provided DOCA-OFED stack,
   not the inbox drivers.
   Refer to the [DAOS Support Matrix](../release/support_matrix.md)
   for information about supported DOCA-OFED releases.

*  The `mercury-ucx` RPM package must be **manually** selected for
   installation. The base `mercury` RPM package ships by default with the
   `mercury-libfabric` package unless `mercury-ucx` is also installed.
   The `mercury-ucx` RPM contains the UCX plugin that is required for
   enabling UCX support.
   This RPM **must** be used in InfiniBand environments when UCX is used.
   Attempts to install this RPM in non-InfiniBand environments
   will fail, because it has a dependency on UCX packages.

*  When installing DAOS, explicitly list the `mercury-ucx` RPM package
   if it was not already installed in the previous step.
   For example, using the `dnf` package manager on EL9:

```bash
      # on DAOS_ADMIN nodes:
      dnf install mercury-ucx daos-admin

      # on DAOS_SERVER nodes:
      dnf install mercury-ucx daos-server

      # on DAOS_CLIENT nodes:
      dnf install mercury-ucx daos-client
```

After UCX support has been enabled by installing the `mercury-ucx`
package, the network provider in the DAOS server's
configuration file (`/etc/daos/daos_server.yml`) should be changed.
A sample YAML file is available on
[GitHub](https://github.com/daos-stack/daos/blob/master/utils/config/examples/daos_server_ucx.yml).
The recommended setting for UCX is `provider: ucx+dc_x`.
