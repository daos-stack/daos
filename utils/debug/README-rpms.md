# General advices

The easiest way to Build custom DAOS RPMs is to use a docker container.
This could dbe done with manually building the docker container or with using a docker compose
file.

## Manual Building


This could be done thanks to the following command:
```
$ cd $DAOS_SRC
$ git switch --recurse-submodules <git branch>
$ docker build . -f <base dockerfile> --build-arg BASE_DISTRO=<base distro> --build-arg DAOS_JAVA_BUILD=no --build-arg COMPILER=gcc --build-arg DAOS_KEEP_SRC=no --build-arg DAOS_DEPS_BUILD=no --build-arg DAOS_BUILD=no --tag <base tag>
$ cd $DAOS_SRC/utils/debug/docker/daos-rpms
$ docker build . -f <builder dockerfile> --build-arg GIT_TAG=<git branch> --tag=<builder tag>
$ dcoker run --rm --volume $PWD/rpms:/mnt/rpms <builder tag> -o /mnt/rpms -t release -b <git branch> |& tee build.log
```

Example of configuration values:
- _git branch_: `ckochhof/fix/v2.4.2-rc3/pr-15799`
- _base dockerfile_: `utils/docker/Dockerfile.leap.15`
- _base distro_: `opensuse/leap:15.5`
- _base tag_: `daos/builder-leap15.5:pr-15799`
- _builder dockerfile_: `Dockerfile-v2.6.4-rc4.daos_rpms_builder.leap.15`

At the end, all the rpms should (i.e. daos and direct dependencies rpms) should
be in the directory `$PWD/rpms`.
