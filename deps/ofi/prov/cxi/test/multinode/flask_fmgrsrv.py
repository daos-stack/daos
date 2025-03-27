#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
# Copyright (c) 2021-2023 Hewlett Packard Enterprise Development LP

help = f'''
Multicast REST server simulation for distributed testing

http://host:port/
- GET produces this help as a JSON list

http://host:port/fabric/collectives/multicast
- POST generates a single multicast address and hwroot node
- GET lists all multicast addresses
- DELETE deletes all multicast addresses

http://host:port/fabric/collectives/mcastid/<id>
- DELETE deletes specified multicast address

Multicast addresses are invalid (>=8192), causing UNICAST behavior
Only addresses 8192-8199 are supported, to test exhaustion
'''
import argparse
import textwrap
import sys
import json

from argparse import ArgumentParser, HelpFormatter
from flask import Flask, request
from flask_restful import Api, Resource

# Global storage for addresses/roots
mcastroots = []
mcastaddrs = []

class RawFormatter(HelpFormatter):
    def _fill_text(self, text, width, indent):
        return "\n".join([textwrap.fill(line, width) for line in textwrap.indent(textwrap.dedent(text), indent).splitlines()])

class fabtestInfo(Resource):
    def get(self):
        return help.splitlines(), 200

def delEntry(value):
    global mcastroots
    global mcastaddrs

    try:
        idx = mcastaddrs.index(value)
        del mcastroots[idx]
        del mcastaddrs[idx]
        print("DELETE ", value)
    except:
        print("multicast", value, "not in use")
        pass

class delete8192(Resource):
    def delete(self):
        delEntry(8192)

class delete8193(Resource):
    def delete(self):
        delEntry(8193)

class delete8194(Resource):
    def delete(self):
        delEntry(8194)

class delete8195(Resource):
    def delete(self):
        delEntry(8195)

class delete8196(Resource):
    def delete(self):
        delEntry(8196)

class delete8197(Resource):
    def delete(self):
        delEntry(8197)

class delete8198(Resource):
    def delete(self):
        delEntry(8198)

class delete8199(Resource):
    def delete(self):
        delEntry(8199)

class fabtestServer(Resource):
    def get(self):
        # Lists the existing multicast addresses
        global mcastroots
        global mcastaddrs

        addrs = []
        for k,v in enumerate(mcastroots):
            addrs.append({'root':v, 'mcast':mcastaddrs[k]})
        info = {
            'ADDRLIST': addrs,
        }
        return info, 200

    def delete(self):
        # Deletes all multicast addresses
        global mcastroots
        global mcastaddrs

        mcastroots = []
        mcastaddrs = []
        return None, 200

    def post(self):
        # Creates a new multicast address
        global mcastroots
        global mcastaddrs

        print(request.json)
        required = {
            'jobID', 'macs', 'timeout',
        }
        optional = {
            'jobStepID'
        }
        info = {}
        error = []
        dupmac = []

        # Test for required fields, append error messages if missing
        for key in required:
            if key not in request.json:
                error.append("no " + key)
            else:
                info[key] = request.json[key]
        # Test macs for empty or duplicate addresses
        if not error and not request.json['macs']:
            error.append('empty macs')
        for mac in request.json['macs']:
            if mac not in dupmac:
                dupmac.append(mac)
            else:
                error.append('duplicate mac=' + str(mac))

        # Test for optional fields, provide defaults if missing
        for key in optional:
            if key not in request.json:
                info[key] = None
            else:
                info[key] = request.json[key]

        # Find a globally-unused mac address as hwRoot
        info['hwRoot'] = None
        for mac in request.json['macs']:
            if mac not in mcastroots:
                info['hwRoot'] = mac
                break
        if not info['hwRoot']:
            error.append('no hwRoot usable')

        # Find a globally unused mcast address
        info['mcastID'] = None
        for adr in range(8192, 8199):
            if adr not in mcastaddrs:
                info['mcastID'] = adr
                break
        if not info['mcastID']:
            error.append('no mcast available')

        # Report any accumulated errors
        if error:
            info = {
                'error' : ', '.join(error)
            }
            return info, 400

        # Otherwise, record and return complete record
        mcastroots.append(mac)
        mcastaddrs.append(adr)

        info['jobID'] = request.json['jobID']
        info['jobStepID'] = request.json['jobStepID']
        info['macs'] = request.json['macs']
        info['timeout'] = request.json['timeout']
        info['documentSelfLink'] = 'fabric/collectives/mcastID/' + adr

        return info, 200

def main(argv):
    parser = argparse.ArgumentParser(
        description=help, formatter_class=RawFormatter)
    parser.add_argument('--host', default=None)
    parser.add_argument('--port', default=None)
    args = parser.parse_args()

    app = Flask(__name__)
    api = Api(app);
    api.add_resource(fabtestInfo, '/')
    api.add_resource(fabtestServer, '/fabric/collectives/multicast')
    api.add_resource(delete8192, '/fabric/collectives/mcastid/8192')
    api.add_resource(delete8193, '/fabric/collectives/mcastid/8193')
    api.add_resource(delete8194, '/fabric/collectives/mcastid/8194')
    api.add_resource(delete8195, '/fabric/collectives/mcastid/8195')
    api.add_resource(delete8196, '/fabric/collectives/mcastid/8196')
    api.add_resource(delete8197, '/fabric/collectives/mcastid/8197')
    api.add_resource(delete8198, '/fabric/collectives/mcastid/8198')
    api.add_resource(delete8199, '/fabric/collectives/mcastid/8199')
    app.run(debug=True, host=args.host, port=args.port)

if __name__ == "__main__":
    main(sys.argv)
