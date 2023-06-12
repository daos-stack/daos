# UCX Fabric Support

For clusters using InfiniBand, DAOS supports [UCX](https://www.openucx.org/)
as an alternative to the default
[libfabric](https://ofiwg.github.io/libfabric/) network stack.
The UCX provider is fully supported since DAOS 2.4.

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
   installation: For the technology preview, the `mercury` package is
   provided in two different versions, which are mutually exclusive:

   - The standard `mercury` RPM does support libfabric,
     but not UCX. This RPM will be installed by default,
     and **must** be used in non-InfiniBand environments.

   - A new `mercury-ucx` RPM is also provided, which supports
     _both_ libfabric and UCX. This RPM **must** be used in
     InfiniBand environments when the intention is to use
     UCX. It **may** also be used in InfiniBand environments
     if the intention is to use libfabric.
     Attempts to install this RPM in non-Infiniband environments
     will fail, because it has a dependency on UCX packages.

*  At DAOS **installation** time, to enable UCX support the new
   `mercury-ucx` RPM package must be explicitly listed in 
   order to prevent the installation of the default `mercury`
   package (which does not include the UCX support).
   For example, using the `yum` package manager on EL8:

```bash
      # on DAOS_ADMIN nodes:
      yum install mercury-ucx daos-admin

      # on DAOS_SERVER nodes:
      yum install mercury-ucx daos-server

      # on DAOS_CLIENT nodes:
      yum install mercury-ucx daos-client
```

*  To **change** an existing DAOS installation from libfabric to
   UCX, the default `mercury` RPM first needs to be un-installed, and
   the `mercury-ucx` RPM must be installed instead. To prevent the
   removal of DAOS altogether (it has a package dependency on mercury),
   the `rpm` command with the `--nodeps` option should be used:

```bash
      # on EL8:
      rpm -e --nodeps mercury
      yum install mercury-ucx

      # on Leap15:
      rpm -e --nodeps mercury
      zypper install mercury-ucx
```

*  To **update** from DAOS 2.0 (with libfabric) to DAOS 2.2 with
   UCX, the recommended path is to first perform a standard DAOS
   RPM update (which will update the default `mercury` package).
   After the update, the `mercury` RPM package can be replaced by
   `mercury-ucx` as described above.

After UCX support has been enabled by installing the `mercury-ucx`
package, the network provider must be changed in the DAOS server's
configuration file (`/etc/daos/daos_server.yml`).
A sample YML file is available on
[github](https://github.com/daos-stack/daos/blob/master/utils/config/examples/daos_server_ucx.yml).
The recommended setting for UCX is `provider: ucx+dc_x`.

