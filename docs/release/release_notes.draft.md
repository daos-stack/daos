# DAOS Version 3.0 Release Notes

THIS IS A DRAFT DOCUMENT, NOT VALID UNTIL DAOS 3.0 GA

## DAOS Version 3.0.0 (YYYY-MM-DD)

We are pleased to announce the release of DAOS version 3.0.

### General Support

The DAOS 3.0.0 release includes the
[daos-3.0.0 RPM packages](https://packages.daos.io/v3.0.0/) and its
prerequisites. DAOS Version 3.0.0 supports the following environments:

Architecture Support:

* DAOS 3.0.0 supports the x86\_64 architecture.

Operating System Support:

* SLES 15 SP6 and SP7
* openSUSE Leap 15.6
* EL 9.7/9.8 (RHEL, Rocky Linux, Alma Linux)

Fabric and Network Provider Support:

* libfabric support for the following fabrics and providers:

    - ofi+tcp on all networking hardware
    - ofi+tcp;ofi\_rxm when RXM is explicitly requested
    - ofi+verbs on fabrics that support the verbs API
    - ofi+cxi on HPE Slingshot fabrics

* [UCX](https://docs.daos.io/v3.0/admin/ucx/) support on NVIDIA InfiniBand and
  RoCE fabrics:

    - ucx+dc\_x is the recommended UCX provider

Storage Class Memory Support (PMem and non-PMem servers):

* DAOS Servers without Intel Optane Persistent Memory, using the production
  Metadata-on-SSD Phase2 code path with allocator v2 as the default for newly
  created pools.
* Existing Metadata-on-SSD Phase1 pools remain supported and retain their
  original allocator when opened.
* DAOS Servers with 3rd gen Intel Xeon Scalable processors and Intel Optane
  Persistent Memory 200 Series.
* DAOS Servers with 2nd gen Intel Xeon Scalable processors and Intel Optane
  Persistent Memory 100 Series.

For a complete list of supported hardware and software, refer to the [Support
Matrix](https://docs.daos.io/v3.0/release/support_matrix/)

### Key features and improvements

#### Software Version Currency

* See [above](#General-Support) for supported operating system levels.

* Refer to the [Support
  Matrix](https://docs.daos.io/v3.0/release/support_matrix/) for supported
  fabric providers and vendor software combinations.

* The following prerequisite software packages used by the DAOS build have been
  updated:

#### New Features and Usability Improvements

##### MD-on-SSD Phase 2

##### Incremental Reintegration Technology Preview

##### Hardlink support

#### Other notable changes

##### Catastrphic Recovery tools

##### Protocol changes for multi-provider support

### Known Issues and Limitations

### Bug fixes

The DAOS 3.0.0 release includes fixes for numerous defects in rebuild, object,
VOS, DTX, erasure coding, pool and container services, DFS, dfuse, `libpil4dfs`,
storage, networking, telemetry, and administrative utilities. For details,
please refer to the Github [release/3.0 commit
history](https://github.com/daos-stack/daos/commits/release/3.0) and the
associated [Jira tickets](https://daosio.atlassian.net/jira) as stated in the
commit messages.

## Additional resources

Visit the [online documentation](https://docs.daos.io/v3.0/) for more
information. All DAOS project source code is maintained in the
[https://github.com/daos-stack/daos](https://github.com/daos-stack/daos)
repository. Please visit this
[link](https://github.com/daos-stack/daos/blob/release/3.0/LICENSE) for more
information on the DAOS license.

Refer to the [System Deployment](https://docs.daos.io/v3.0/admin/deployment/)
section of the [DAOS Administration
Guide](https://docs.daos.io/v3.0/admin/hardware/) for installation details.
