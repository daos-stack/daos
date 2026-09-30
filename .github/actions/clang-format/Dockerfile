FROM fedora:43@sha256:781b7642e8bf256e9cf75d2aa58d86f5cc695fd2df113517614e181a5eee9138

RUN dnf -y install clang-tools-extra git-clang-format

COPY entrypoint.sh /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
