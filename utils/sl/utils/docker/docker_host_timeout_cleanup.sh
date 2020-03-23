#!/bin/bash

set -x

container=$(cat "${WORKSPACE}/docker_container_name.txt")

docker rm -f "${container}"  || true

