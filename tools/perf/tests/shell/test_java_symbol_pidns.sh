#!/bin/bash
# Test symbol resolution for java running inside a PID namespace
# SPDX-License-Identifier: GPL-2.0

set -u -o pipefail

# shellcheck source=lib/setup_libjvmti.sh
. "$(dirname "$0")/lib/setup_libjvmti.sh"

JSHELL=(jshell -s -J"-agentpath:$LIBJVMTI")
if ! command -v jshell &>/dev/null; then
	echo "skip: no jshell, install JDK"
	exit 2
fi

UNSHARE=(
	unshare
	--user --map-user="$(id -u)" --map-group="$(id -g)"
	--fork --pid --mount-proc
)
if ! "${UNSHARE[@]}" true; then
	echo "skip: unshare not functional"
	exit 2
fi

if [ "$#" -eq 0 ]; then
	# Verify isolation by running the test inside a separate PID namespace.
	exec "${UNSHARE[@]}" "$0" "$@" stage2
fi
if [ "$1" != stage2 ]; then
	echo "$0 must be started without arguments"
	exit 1
fi
shift

WORKDIR=$(mktemp -d /tmp/__perf_test_java_symbol_pidns.XXXXX)
trap 'rm -r "$WORKDIR"' EXIT

test() {
	TEST_ID=jshell-exit-$JSHELL_EXIT

	# Verify that perf inject can deal with JIT dump produced by jshell
	# running in PID namespace that is different from its own one.
	# Make sure jshell is still around when perf inject runs, so attach and
	# detach perf manually.
	# jshell is a shell script that runs java as a child process, and perf
	# cannot attach to existing children. Therefore create a stopped
	# process, which will exec jshell after it's resumed.
	JSHELL_STDIN=$WORKDIR/jshell-stdin-$TEST_ID
	JSHELL_STDOUT=$WORKDIR/jshell-stdout-$TEST_ID
	mkfifo "$JSHELL_STDIN"
	mkfifo "$JSHELL_STDOUT"
	exec {JSHELL_STDIN_FD}<>"$JSHELL_STDIN"
	exec {JSHELL_STDOUT_FD}<>"$JSHELL_STDOUT"
	JITDUMPDIR="$WORKDIR" bash -c 'kill -STOP "$$" && exec "$0" "$@"' \
		"${UNSHARE[@]}" "${JSHELL[@]}" \
		<&"$JSHELL_STDIN_FD" >&"$JSHELL_STDOUT_FD" &
	JSHELL_PID=$!

	# Start perf record and wait until it attaches to jshell.
	PERF_CTL=$WORKDIR/perf-ctl-$TEST_ID
	PERF_ACK=$WORKDIR/perf-ack-$TEST_ID
	mkfifo "$PERF_CTL"
	mkfifo "$PERF_ACK"
	exec {PERF_CTL_FD}<>"$PERF_CTL"
	exec {PERF_ACK_FD}<>"$PERF_ACK"
	PERF_DATA=$WORKDIR/perf-$TEST_ID.data
	perf record --clockid=1 --delay=-1 \
		--control="fd:$PERF_CTL_FD,$PERF_ACK_FD" \
		--output="$PERF_DATA" --pid="$JSHELL_PID" &
	PERF_PID=$!
	echo "enable" >&"$PERF_CTL_FD"
	while IFS= read -r LINE <&"$PERF_ACK_FD"; do
		[ "$LINE" != "ack" ] || break
	done

	# Spawn jshell and ask it to run some CPU-intensive code.
	kill -CONT "$JSHELL_PID"
	cat >&"$JSHELL_STDIN_FD" <<EOF
int fib(int x) { return x > 1 ? fib(x - 2) + fib(x - 1) : 1; }
System.out.println(fib(44));
EOF
	while IFS= read -r LINE <&"$JSHELL_STDOUT_FD"; do
		[ "$LINE" != 1134903170 ] || break
	done

	# Terminate perf record.
	echo "stop" >"$PERF_CTL"
	if ! wait "$PERF_PID"; then
		echo "Fail to record for java program"
		exit 1
	fi

	# Terminate jshell before perf inject.
	if [ "$JSHELL_EXIT" = "early" ]; then
		echo "/exit" >&"$JSHELL_STDIN_FD"
		if ! wait "$JSHELL_PID"; then
			echo "Fail to run java program"
			exit 1
		fi
	fi

	# Inject the JIT data.
	PERF_INJ_DATA=$WORKDIR/perf-$TEST_ID.data.inj
	if ! DEBUGINFOD_URLS="" perf inject --input="$PERF_DATA" \
		--output="$PERF_INJ_DATA" --jit; then
		echo "Fail to inject samples"
		exit 1
	fi

	# Below is an example of the instruction samples reporting:
	#   8.18%  jshell           jitted-50116-29.so    [.] Interpreter
	#   0.75%  Thread-1         jitted-83602-1670.so  [.] jdk.internal.jimage.BasicImageReader.getString(int)
	# Look for them, while avoiding false positives from lines like this:
	#   0.03%  jshell           libjvm.so             [.] InterpreterRuntime::resolve_get_put(JavaThread*, Bytecodes::Code)
	if ! perf report --input="$PERF_INJ_DATA" --stdio |
		grep ' jshell .* jitted-.*\.so .* \(Interpreter$\|jdk\.internal\)' \
			&>/dev/null; then
		echo "Fail to find java symbols"
		exit 1
	fi

	# Terminate jshell after perf inject.
	if [ "$JSHELL_EXIT" = "late" ]; then
		echo "/exit" >&"$JSHELL_STDIN_FD"
		if ! wait "$JSHELL_PID"; then
			echo "Fail to run java program"
			exit 1
		fi
	fi
}

for JSHELL_EXIT in early late; do
	test
done
