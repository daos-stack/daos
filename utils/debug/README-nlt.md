# General advices

NLT tests are generating the following files:
- $DAOS\_SRC/nlt-errors.json: error in the stdouput and log files
- $DAOS\_SRC/nlt-server-leaks.json: server memory leaks detected
- /tmp/dnt\_*.log.bz2: output of the NLT commands.

The last log files can be used for discovering the NLT test in utils which is failing.
```bzgrep -e "ERROR MESSAGE" /tmp/dnt_*.log.bz2```

The list of the test can be found either by running the NLT launcher with the `--test=list` option,
or by looking into the file `utils/node_local_test.py` and search for function prefixed with
`test_`.

With CI, the NLT tests are run in parallel.   If one test is failing the error message to look at is
`test_<name of the test> Failed`

NLT test can be run in different modes.  There is not yet documentation on the available modes: the
source code of the NLT tests is the documentation.  The fi mode allow to run Fault Injection tests.
