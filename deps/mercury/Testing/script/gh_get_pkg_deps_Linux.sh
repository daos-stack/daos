#!/bin/bash
echo "$1" | sed "s/,/ /g" | xargs -n 1 apt-cache depends
