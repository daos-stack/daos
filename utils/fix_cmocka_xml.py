#!/usr/bin/env python
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

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.

'''
import subprocess
import os

# Some fixes for the CMOCKA headers
xml_header ="<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n"
parent_header = "<testsuites>\n"
parent_footer = "</testsuites>\n"
path = os.path.join(os.path.dirname(os.path.realpath(__file__)),"../test_results/*.xml")
cmd = "ls {}".format(path)
out = subprocess.check_output(cmd,shell=True)
# This is done because some XML files are not formed correctly
# by CMOCKA framework. Some files doesn't have <xml..> or
# <root> having nested tags [eg: if one group test called
# repeatedly].
# If there is a fix by CMOCKA framework, this is not required.
files = out.split('\n')
#Two pass update on the files
#Remove all testsuites tag out of the xml file.
for file in files:
	if (file != ""):
		print(file)
		file_handle =  open('{0}'.format(file), "r+")
		lines = file_handle.readlines()
		lines = [tmp.strip(' ') for tmp in lines]
		while parent_footer in lines : lines.remove(parent_footer)
		while parent_header in lines : lines.remove(parent_header)
		file_handle.truncate(0)
		file_handle.seek(0)
		file_handle.writelines(lines)
		file_handle.close()
#Reconstruct the header and footer
for file in files:
	if (file != ""):
		file_handle =  open('{0}'.format(file), "r+")
		lines = file_handle.readlines()
		print(lines[0])
		if("xml" in lines[0]):
			lines[0] = lines[0].replace(lines[0],lines[0]+parent_header)
		else:
			lines[0] = lines[0].replace(lines[0],xml_header+parent_header+lines[0])
		file_handle.truncate(0)
		file_handle.seek(0)
		file_handle.writelines(lines)
		file_handle.seek(0,os.SEEK_END)
		file_handle.writelines(parent_footer)
		file_handle.close()
