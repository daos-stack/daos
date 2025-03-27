#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
# Copyright (c) 2021 Hewlett Packard Enterprise Development LP
help = f'''
Standalone REST server for local testing

TARGET /test
    Provides basic targets for GET, PUT, POST, PATCH, and DELETE.
    "Content-Type: application/json" header should be specified.
    Result is JSON data identifying the operation, and the supplied data.
    If the supplied data contains a JSON tag named 'return_code',
        the corresponding value will be used as the return code of the
        response.
    Exercise using ./curltest --auto

If --host is omitted, host is http://127.0.0.1 local address (if available)
If --host is 0.0.0.0, host is the current IP address of the node
'''

import argparse
import textwrap
import sys
import json

from argparse import ArgumentParser, HelpFormatter
from flask import Flask, request
from flask_restful import Api, Resource

class RawFormatter(HelpFormatter):
    def _fill_text(self, text, width, indent):
        return "\n".join([textwrap.fill(line, width) for line in textwrap.indent(textwrap.dedent(text), indent).splitlines()])

# Test code for CURL regression test
class selftestResource(Resource):
    def return_code(self, json):
        if json is not None and "return_code" in json:
            return json["return_code"]
        return 200

    def get(self):
        info = {
            'operation': 'GET',
            'data': ''
        }
        return info, self.return_code(None)

    def put(self):
        info = {
            'operation': 'PUT',
            'data': request.json
        }
        return info, self.return_code(request.json)

    def post(self):
        info = {
            'operation': 'POST',
            'data': request.json
        }
        return info, self.return_code(request.json)

    def patch(self):
        info = {
            'operation': 'PATCH',
            'data': request.json
        }
        return info, self.return_code(request.json)

    def delete(self):
        info = {
            'operation': 'DELETE',
            'data': request.json
        }
        return info, self.return_code(request.json)

def main(argv):
    parser = argparse.ArgumentParser(
        description=help, formatter_class=RawFormatter)
    parser.add_argument('--host', default=None)
    parser.add_argument('--port', default=None)
    args = parser.parse_args()

    app = Flask(__name__)
    api = Api(app);
    api.add_resource(selftestResource, '/test')
    app.run(debug=True, host=args.host, port=args.port)

if __name__ == "__main__":
    main(sys.argv[1:])
