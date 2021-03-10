# Go language bindings for the IPMCTL API

Go bindings for the [ipmctl](https://github.com/intel/ipmctl) native-C library
to facilitate management of PMem (Intel(R) Optane(TM) persistent memory)
modules from an application written in Go.

The bindings require libipmctl-devel (or distro equivalent) package to be
installed.
To install please follow steps in the
[ipmctl github instructions](https://github.com/intel/ipmctl).

These bindings are currently working against ipmctl 2.x.

This is not a general purpose set of ipmctl go bindings but provides a set of
capabilities tailored to the specific needs of DAOS, the PMem related features
are as follows:

* device discovery
* device firmware version discovery
* device firmware update

Functionality is exposed through the package's `IpmCtl` public interface.
