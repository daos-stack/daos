# General advices

The easiest way to Build custom DAOS RPMs is to use a docker container.
This could dbe done with manually building the docker container or with using a docker compose
file.

## Manual Building


This could be done thanks to the following command:
```
$ cd $DAOS_SRC
$ git switch --recurse-submodules <target_branch>
$ docker build . -f <base dockerfile> --build-arg BASE_DISTRO=<base distro> --build-arg DAOS_JAVA_BUILD=no --build-arg COMPILER=gcc --build-arg DAOS_KEEP_SRC=no --build-arg DAOS_DEPS_BUILD=no --build-arg DAOS_BUILD=no --tag <base tag>
$ cd $DAOS_SRC/utils/debug/docker/bkp
$ docker build . -f <builder dockerfile> --build-arg GIT_BRANCH=<git branch> --tag=<builder tag>
$ docker run --rm -ti <builder tag> bash
$ cd daos-build/utils/rpms
$ bash create-tarball.sh <archive name>
```

Example of configuration values:
- _base dockerfile_: `utils/docker/Dockerfile.leap.15`
- _base distro_: `opensuse/leap:15.5`
- _base tag_: `daos/builder-leap15.5:v2.4`
- _builder dockerfile_: `Dockerfile.daos_builder.leap.15`
- _git branch_: `ckochhof/fix/v2.4.2-rc3/daos-15799`
- _target tag_: `daos/builder-leap14.5:daos-15799`
- _builder tag_: `daos/builder-leap15.5:daos-15799`
- _archive name_: name of the archive containing the RPMs

# With Docker Compose


