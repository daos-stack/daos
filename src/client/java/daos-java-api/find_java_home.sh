java_home=$JAVA_HOME
if [ -n  "$java_home" ] 
then 
	echo $java_home
else
	java_path=$(dirname $(readlink -f $(which java)))
	java_home=$(echo $java_path | sed 's^/jre/bin^^')
	if  [ ! -f "${java_home}/include/jni.h" ] 
	then
		java_home=$(echo $java_path | sed 's^/bin^^')
		if [ ! -f "$java_home/include/jni.h" ]
		then
			exit 1
		fi
	fi
	echo $java_home
fi
