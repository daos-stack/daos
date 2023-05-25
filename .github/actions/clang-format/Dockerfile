FROM fedora:38

RUN dnf -y install clang-tools-extra

RUN clang-format --version

ENTRYPOINT ["/entrypoint.sh"]
