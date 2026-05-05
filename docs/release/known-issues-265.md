
DAOS 2.6.5 Known Issues

An issue with memory registration handling in the libfabric cxi provider
may cause DER\_NOMEM errors during rebuild. This issue is fixed in
libfabric PR https://github.com/ofiwg/libfabric/pull/11908,
which has been landed in libfabric but is not included in the libfabric
version shipped with the current Slingshot Host Stack (SHS).
The workaround is to install the latest libfabric, which includes
this PR (DAOS-18326).

