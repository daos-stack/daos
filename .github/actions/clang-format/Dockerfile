FROM fedora:38

RUN dnf -y install clang-tools-extra git-clang-format

RUN clang-format --version

COPY entrypoint.sh /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
