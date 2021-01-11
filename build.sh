#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

cd ${DIR}

C_FLAGS="-fPIC -O0 -g -Wall -Werror -Wno-format-truncation"
LD_FLAGS="-shared -fPIC -lyed"

pids=()

mkdir -p obj

for f in src/*.c; do
    echo "CC    $f"
    gcc -c ${C_FLAGS} $f -o obj/$(basename $f .c).o &
    pids+=($!)
done

for p in ${pids[@]}; do
    wait $p || exit 1
done

echo "LD    chatty_kathy.so"
gcc ${LD_FLAGS} -o chatty_kathy.so obj/*.o || exit 1

echo "Done."
