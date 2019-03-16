#!/bin/bash -x

UID=$1
[ -z $UID ] && UID=`uname -n`

HOST_SYSTEM=`uname -s`
COMPONENT=brubeck_runtime

INVIDIUSER=dbittner
INVIDIAKEY=AKCp5cbwdcFgSBXs43J98SYgHgDZhANM7Pot3yha6f9FMQjEeMnSeqT3DyCiyf5YmFTfXC4TT

rm -f ${COMPONENT}.${HOST_SYSTEM}.tar.gz
curl -u ${INVIDIUSER}:${INVIDIAKEY} -O "http://jfrog.invidi.com:80/artifactory/rwi-files-local/latest/${COMPONENT}.${HOST_SYSTEM}.tar.gz"
tar xvzf ${COMPONENT}.${HOST_SYSTEM}.tar.gz
pushd ${COMPONENT}

[ -f /usr/local/sbin/brubeck ] && mv /usr/local/sbin/brubeck /usr/local/sbin/brubeck.old
[ ! -d /usr/local/sbin ] && mkdir -p /usr/local/sbin
cp brubeck /usr/local/sbin/brubeck
chown root.root /usr/local/sbin/brubeck
chmod +x /usr/local/sbin/brubeck

[ -f /etc/init.d/brubeck ] && mv /etc/init.d/brubeck /etc/init.d/brubeck.old
cp brubeck_init_alpine.d /etc/init.d/brubeck
chown root.root /etc/init.d/brubeck
chmod +x /etc/init.d/brubeck

function set()
{
   MUNGE=$1
   KEY=$2
   VALUE=$3
   FILE=$4
   INT=$5

   if [ $INT -gt 0 ] ; then
	sed -i -e "s/${MUNGE}/            \"${KEY}\" : ${VALUE}\,/g" $FILE
   else
	sed -i -e "s/${MUNGE}/            \"${KEY}\" : \"${VALUE}\"\,/g" $FILE
   fi
}

[ -f /etc/brubeck/config.json ] && mv /etc/brubeck/config.json /etc/brubeck/config.json.old

CONFIG_WORK="/tmp/config.json"
 rm -f "${CONFIG_WORK}"
cat <<EOF >> "${CONFIG_WORK}"
{
    "sharding" : false,
SERVER_NAME
    "dumpfile" : "/var/log/brubeck.dump",
    "capacity" : 2000,
    "backends" :
    [
	{
	    "type" : "carbon",
CARBON_SERVER
	    "port" : 2003,
	    "frequency" : 10,
	    "pickle" : false,
	    "expire" : 1
	}
    ],
    "samplers" :
    [
	{
	    "type" : "statsd",
	    "address" : "0.0.0.0",
BRUBECK_PORT
	    "workers" : 8,
	    "multisock" : false,
	    "multimsg" : 0
	}
    ]
}
EOF
       
set "SERVER_NAME" "server_name" "brubeck-${UID}" "${CONFIG_WORK}" 0
#set "CARBON_SERVER" "address" "bubble.bottorrent.net" "${CONFIG_WORK}" 0
set "CARBON_SERVER" "address" "10.120.95.115" "${CONFIG_WORK}" 0
set "BRUBECK_PORT" "port" 8125 "${CONFIG_WORK}" 1

[ ! -d /etc/brubeck ] && mkdir -p /etc/brubeck
cp "${CONFIG_WORK}" /etc/brubeck/config.json
chown -R root.root /etc/brubeck/config.json

popd
rm -rf ${COMPONENT}
rm -f ${COMPONENT}.${HOST_SYSTEM}.tar.gz
