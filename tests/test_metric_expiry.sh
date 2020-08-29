#!/bin/bash
# Test that a counter metric is only sent at the end of reporting intervals
# where a statsd entry for it was received.
set -ueo pipefail

[[ -x ./brubeck ]] || {
    echo "brubeck executable expected to be in the working directory" >&2
    exit 1
}

statsd_port=8130
carbon_port=2003
server_name=brubeck_debug
metric=a.count

carbon_frequency=1  # Report every second for faster feedback.
brubeck_config=$(cat <<EOF
{
  "sharding" : false,
  "server_name" : "$server_name",
  "dumpfile" : "./brubeck.dump",
  "capacity" : 15,
  "backends" : [
    {
      "type" : "carbon",
      "address" : "127.0.0.1",
      "port" : $carbon_port,
      "frequency" : $carbon_frequency
    }
  ],
  "samplers" : [
    {
      "type" : "statsd",
      "address" : "0.0.0.0",
      "port" : $statsd_port,
      "workers" : 1,
      "multisock" : true,
      "multimsg" : 8
    }
  ]
}
EOF
)

# Listen for carbon and filter out the brubeck internal metrics.
nc -l "$carbon_port" | grep "$metric" >carbon.log &

: >brubeck.log
./brubeck --log brubeck.log --config <(echo "$brubeck_config") &

# Wait for brubeck to be listening on $statsd_port before sending a metric.
while true; do
    grep 'sampler=statsd event=worker_online' brubeck.log >/dev/null && break
    sleep 0.1
done
nc -4 -u -w0 localhost "$statsd_port" <<<"$metric:1|c"

sleep 3

# Seems like if we send SIGINT that grep writes everything that it has buffered
# before exiting, whereas it stops abruptly without writing if receiving
# SIGTERM.
kill -INT %nc %./brubeck
wait

count=$(grep -c "$metric" carbon.log)
if ((count > 1)); then
    echo "$metric sent via carbon more than once. See carbon.log"
    exit 1
elif ((count == 0)); then
    echo "$metric not seen in carbon.log"
    exit 1
fi

rm brubeck.log carbon.log
