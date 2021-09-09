Build from scratch
==================

Install pre-requisites
----------------------

CentOS

**git clone**

\# Run as root

yum clean all && \\

yum -y install epel-release && \\

yum -y upgrade && \\

yum -y install \\

boost-python36-devel \\

clang-analyzer \\

cmake \\

CUnit-devel \\

doxygen \\

e2fsprogs \\

file \\

flex \\

fuse3-devel \\

gcc \\

gcc-c++ \\

git \\

golang \\

graphviz \\

hwloc-devel \\

ipmctl \\

java-1.8.0-openjdk \\

json-c-devel \\

lcov \\

libaio-devel \\

libcmocka-devel \\

libevent-devel \\

libipmctl-devel \\

libiscsi-devel \\

libtool \\

libtool-ltdl-devel \\

libunwind-devel \\

libuuid-devel \\

libyaml-devel \\

Lmod \\

lz4-devel \\

make \\

man \\

maven \\

nasm \\

ndctl \\

numactl-devel \\

openssl-devel \\

pandoc \\

patch \\

patchelf \\

pciutils \\

python3-pip \\

python36-Cython \\

python36-devel \\

python36-distro \\

python36-jira \\

python36-numpy \\

python36-paramiko \\

python36-pylint \\

python36-requests \\

python36-requests \\

python36-tabulate \\

python36-pyxattr \\

python36-scons \\

sg3\_utils \\

valgrind-devel \\

yasm && \\

yum clean all

yum -y install openmpi3-devel && yum clean all

SuSE

**git clone**

\# Run as root

zypper \--non-interactive update && \\

zypper \--non-interactive install \\

boost-devel \\

clang \\

cmake \\

cunit-devel \\

curl \\

doxygen \\

flex \\

fuse3-devel \\

gcc \\

gcc-c++ \\

git \\

graphviz \\

gzip \\

hwloc-devel \\

java-1\_8\_0-openjdk-devel \\

libaio-devel \\

libcmocka-devel \\

libevent-devel \\

libiscsi-devel \\

libjson-c-devel \\

libltdl7 \\

liblz4-devel \\

libnuma-devel \\

libopenssl-devel \\

libtool \\

libunwind-devel \\

libuuid-devel \\

libyaml-devel \\

lua-lmod \\

make \\

man \\

maven \\

nasm \\

openmpi3-devel \\

pandoc \\

patch \\

patchelf \\

python3-devel \\

python3-distro \\

python3-pip \\

python3-pyxattr \\

python3-PyYAML \\

python3-tabulate \\

scons \\

sg3\_utils \\

valgrind-devel \\

which \\

yasm && \\

zypper clean \--all

update-ca-certificates

zypper \--non-interactive \--no-gpg-checks install \--allow-unsigned-rpm
lua-lmod

zypper \--non-interactive install -y ipmctl-devel go1.14 go1.14-race

Daos source code
----------------

Download daos source code using the following command:

**git clone**

git clone \--recurse-submodules -b v1.2-rc1
https://github.com/daos-stack/daos.git

Building DAOS & Dependencies
----------------------------

Once the sources are downloaded, the pre-requisites to build the source
code can be installed by:

Note:

CentOS

**git clone**

cd daos

scons-3 \--config=force \--build-deps=yes install

SuSE

**git clone**

cd daos

scons \--config=force \--build-deps=yes install
