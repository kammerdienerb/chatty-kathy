#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

cd ${DIR}

cp chatty_kathy.so ~/.yed/chatty_kathy.so.new
mv ~/.yed/chatty_kathy.so.new ~/.yed/chatty_kathy.so
