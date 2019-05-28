# DAOS ISC Demo

<a href="py/demo.py">The demo.py</a> python script provides the main interface
to set up the DAOS environment (i.e. set env variables, create pool and
container) to run the IOR workload. The IOR demo is composed of 3 phases:
- Run IOR with 256B transfer size
- Trigger aggregation to show that space usage is transfered from SCM to SSD
- Run IOR with 1MiB transfer size and show that SSD space usage grows

This script starts a background thread (i.e. space_mon()) that runs pool query
every 2s to store the result in an InfluxDB which is then consumed by Grafana.

The ior performance is collected by parsing the output.

The 2nd workload is PLAsTiCC to be run in Spark.

For the demo, we will use Jupyter notebooks to fire the test. The <a
href="notebook/">notebook</a> directory includes the notebooks for both IOR
and the PLAsTiCC workload.
