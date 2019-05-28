#!/usr/bin/python
'''
  (C) Copyright 2019 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
'''

from __future__ import print_function
from influxdb import InfluxDBClient

class influxdb(object):
    """influxdb main class
    """
    def __init__(self):
        self.host = 'wolf-75'
        self.port = 8086
        self.dbname = 'ior_data'
        self.dbuser = 'daos'
        self.dbuser_password = 'daos'
        self.client = None

    def connect(self):
        self.client = InfluxDBClient(self.host,
                                     self.port,
                                     self.dbuser,
                                     self.dbuser_password,
                                     self.dbname)

    def write_point(self, value):
        rccode = self.client.write_points(value)
        if rccode is False:
            print ("Failed to write point {}".format(value))

    def write_ior_output(self, cmd_output):
        stats = cmd_output.split()
        _json_body = [
        {
            "measurement": "ior_output",
             "tags": {
                "operation": stats[0],
            },
            "fields": {
                "BW": float(stats[1]),
                "IOPS": float(stats[2]),
                "Latency": float(stats[3]),
            }
        }
        ]
        if len(stats) <= 11:
            self.write_point(_json_body)

    def write_ior_input(self, **data):
        _json_body = [
        {
            "measurement": "ior_input",
            "fields": {
                "transfer_size": data['size'],
                "block_size": data['ior_bsize'],
                "client_process": data['ior_tasks'],
            }
        }
        ]
        self.write_point(_json_body)

    def write_storage_info(self, storage):
        _json_body = [
        {
            "measurement": "storage",
            "fields": {
                "AEP_Total": storage['aep_total'],
                "AEP_Used": storage['aep_used'],
                "AEP_ratio": storage['aep_ratio'],
                "SSD_Total": storage['ssd_total'],
                "SSD_Used": storage['ssd_used'],
                "SSD_ratio": storage['ssd_ratio'],
            }
        }
        ]
        self.write_point(_json_body)

    def write_zero_point(self):
        _json_body = [
        {
            "measurement": "ior_input",
            "fields": {
                "transfer_size": 0,
                "block_size": 0,
                "client_process": 0,
            }
        }
        ]
        self.write_point(_json_body)
        self.write_ior_output("write 0 0 0")
        self.write_ior_output("read 0 0 0")