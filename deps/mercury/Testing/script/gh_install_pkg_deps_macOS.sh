#!/bin/bash
PKG=$(echo "$1" | sed "s/,/ /g")
# brew update
brew install $PKG
# brew cleanup

