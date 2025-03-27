Testing
=======

The tests in libvfio-user are organized into a number of suites

* unit - old unit tests written in C
* functional - old functional tests using binaries written in C
* pyunit - modern tests written in python
* style - code style checking (eg flake8, rstlint, etc)

Running `meson test -C build` runs all the suites:

```
meson build
meson test -C build
```

It is possible to be selective about which tests are run
by including or excluding suites:

```
meson test -C build --suite=pyunit
meson test -C build --no-suite=pyunit
```

The `unit` and `pyunit` suites support optional execution
under valgrind

```
meson test -C build --suite=unit --setup=valgrind
meson test -C build --suite=pyunit --setup=pyvalgrind
```

To run with ASAN enabled, pass the `-Db_sanitize=address`
option to meson. Note, this is incompatible with enabling
valgrind

```
meson build -Db_sanitize=address
meson test -C build
```

The `.github/workflows/pull_request.sh` script runs a
sequence of builds in various configurations. This is
invoked for all pull requests, but can be launched
manually by contributors ahead of opening a pull
request.

The master branch is run through [Coverity](scan.coverity.com) when a new PR
lands.

Coverage reports can be enabled via meson, provided `gcovr` is installed:

```
meson build -Db_coverage=true
meson test -C build
ninja -C build coverage
```

The resulting coverage report can be viewed with

```
firefox file:`pwd`/build/meson-logs/coveragereport/index.html
```

Debugging Test Errors
---------------------

Sometimes debugging Valgrind errors on Python unit tests can be tricky. To
run specific tests, pass their name on the command line to `meson`:

```
meson build
meson test -C build test_quiesce.py
```

AFL++
-----

You can run [American Fuzzy Lop](https://github.com/AFLplusplus/AFLplusplus)
against `libvfio-user`. It's easiest to use the Docker container:

```
cd /path/to/libvfio-user/src
docker pull aflplusplus/aflplusplus
docker run -ti -v $(pwd):/src aflplusplus/aflplusplus
```

Set up and build:

```
apt update
apt-get -y install libjson-c-dev libcmocka-dev clang valgrind \
                   python3-pytest debianutils flake8 meson

cd /src
export AFL_LLVM_LAF_ALL=1
CC=afl-clang-fast meson build -Dtran-pipe=true
ninja -C build

mkdir inputs
# don't yet have a better starting point
echo "1" >inputs/start
mkdir outputs
```

The `VFU_TRAN_PIPE` is a special `libvfio-user` transport that reads from
`stdin` instead of a socket, we'll use this with the sample server to do our
fuzzing:

```
afl-fuzz -i inputs/ -o outputs/ -- ./build/dbg/samples/server pipe
```
