#!/bin/bash

request() {
  curl -s localhost:8008/sleep > /dev/null
  echo $1
}

for i in $(seq 0 10); do
  request $i &
done
wait
