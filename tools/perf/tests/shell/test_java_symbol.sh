#!/bin/bash
# Test java symbol

# SPDX-License-Identifier: GPL-2.0
# Leo Yan <leo.yan@linaro.org>, 2022

# Allow [ $? -ne 0 ], because long commands look ugly in if statements.
# shellcheck disable=SC2181

# skip if there's no jshell
if ! [ -x "$(command -v jshell)" ]; then
	echo "skip: no jshell, install JDK"
	exit 2
fi

PERF_DATA=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
PERF_INJ_DATA=$(mktemp /tmp/__perf_test.perf.data.inj.XXXXX)

# Shellcheck does not understand that this function is used by a trap.
# shellcheck disable=SC2317
cleanup_files() {
	echo "Cleaning up files..."
	rm -f "$PERF_DATA"
	rm -f "$PERF_INJ_DATA"
}

trap cleanup_files exit term int

# shellcheck source=lib/setup_libjvmti.sh
. "$(dirname "$0")/lib/setup_libjvmti.sh"

cat <<EOF | perf record -k 1 -o "$PERF_DATA" jshell -s -J"-agentpath:$LIBJVMTI"
int fib(int x) {
	return x > 1 ? fib(x - 2) + fib(x - 1) : 1;
}

int q = 0;

for (int i = 0; i < 10; i++)
	q += fib(i);

System.out.println(q);
EOF

if [ $? -ne 0 ]; then
	echo "Fail to record for java program"
	exit 1
fi

if ! DEBUGINFOD_URLS='' perf inject -i "$PERF_DATA" -o "$PERF_INJ_DATA" -j; then
	echo "Fail to inject samples"
	exit 1
fi

# Below is an example of the instruction samples reporting:
#   8.18%  jshell           jitted-50116-29.so    [.] Interpreter
#   0.75%  Thread-1         jitted-83602-1670.so  [.] jdk.internal.jimage.BasicImageReader.getString(int)
# Look for them, while avoiding false positives from lines like this:
#   0.03%  jshell           libjvm.so             [.] InterpreterRuntime::resolve_get_put(JavaThread*, Bytecodes::Code)
perf report --stdio -i "$PERF_INJ_DATA" 2>&1 |
	grep ' jshell .* jitted-.*\.so .* \(Interpreter$\|jdk\.internal\)' &>/dev/null

if [ $? -ne 0 ]; then
	echo "Fail to find java symbols"
	exit 1
fi

exit 0
