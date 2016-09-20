#!/usr/bin/bash


function yum_check {
 yum -y install fuse fuse-devel openssl-devel
 yum -y install python34 python34-devel
 yum -y install python34-libs python34-tools
 yum -y install PyYAML libyaml
 yum -y install valgrind
 yum -y install libffi-devel
}

function pip3_check {
  hostname
  if [ ! -e /usr/bin/python3 ] ; then
    ln -s /usr/bin/python3.4 /usr/bin/python3
  else
    echo "python3 link exists."
  fi

  if [ ! -e /usr/bin/pip3 ] ; then
    curl -k "https://bootstrap.pypa.io/get-pip.py" -o "get-pip.py"
    python3 get-pip.py
    cp /usr/bin/pip2 /usr/bin/pip

    pip_additions="virtualenv pyopenssl ndg-httpsclient pyasn1 "
    pip_additions+="flake8 sphinx pytest pytest-cov gcovr requests pylint "
    pip_additions+="astroid pyyaml numpy"
    pip3 install -U ${pip_additions}
  else
    echo "pip3 exists."
  fi
}

yum_check
pip3_check
