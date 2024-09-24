#!/usr/bin/env bash


java_home_path=$JAVA_HOME
if [ -n  "$java_home_path" ]
then
	echo "$java_home_path"
else
	java_path="$(dirname "$(readlink -f "$(command -v java)")")"
	java_home_path="${java_path/\/jre\/bin/}"
	if  [ ! -f "${java_home_path}/include/jni.h" ]
	then
		java_home_path="${java_path/\/bin/}"
		if [ ! -f "$java_home_path/include/jni.h" ]
		then
			exit 1
		fi
	fi
	echo "$java_home_path"
fi
