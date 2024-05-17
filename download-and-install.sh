#!/bin/sh
set -euxv
: ${BRANCH:=hwloc-xen-topology-master-v7}

yum-config-manager --add-repo https://koji.eng.citrite.net/kojifiles/repos/rt-next/latest/x86_64
yum install -y --nogpgcheck git gcc make libtool automake xen-dom0-libs-devel clang16
git clone -b $BRANCH --depth=1 https://github.com/xenserver-next/hwloc.git $BRANCH
cd $BRANCH
sh autogen.sh && ./configure --enable-xen --enable-debug && make -j$(nproc)
export HWLOC_DEBUG_VERBOSE=0
utils/lstopo/lstopo-no-graphics
