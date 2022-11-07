#!/bin/bash

HOST_SYSTEM=`uname -s`
COMPONENT=brubeck_runtime

rm -f ${COMPONENT}.${HOST_SYSTEM}.tar.gz
wget http://bubble.bottorrent.net/static/RWI/runtime/${COMPONENT}.${HOST_SYSTEM}.tar.gz
tar xvzf ${COMPONENT}.${HOST_SYSTEM}.tar.gz
pushd ${COMPONENT}
./install.sh
popd
rm -rf ${COMPONENT}
rm -f ${COMPONENT}.${HOST_SYSTEM}.tar.gz



