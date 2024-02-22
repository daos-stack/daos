#!/bin/bash -e

cd daos

./utils/run_utest.py --gha --sudo no --no-fail-on-error

