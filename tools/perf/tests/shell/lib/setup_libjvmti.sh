#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

if [ -e "$PWD/tools/perf/libperf-jvmti.so" ]; then
	LIBJVMTI=$PWD/tools/perf/libperf-jvmti.so
elif [ -e "$PWD/libperf-jvmti.so" ]; then
	LIBJVMTI=$PWD/libperf-jvmti.so
elif [ -e "$PREFIX/lib64/libperf-jvmti.so" ]; then
	LIBJVMTI=$PREFIX/lib64/libperf-jvmti.so
elif [ -e "$PREFIX/lib/libperf-jvmti.so" ]; then
	LIBJVMTI=$PREFIX/lib/libperf-jvmti.so
elif [ -e "/usr/lib/linux-tools-$(uname -a | awk '{ print $3 }' | sed -r 's/-generic//')/libperf-jvmti.so" ]; then
	LIBJVMTI=/usr/lib/linux-tools-$(uname -a | awk '{ print $3 }' | sed -r 's/-generic//')/libperf-jvmti.so
else
	echo "Fail to find libperf-jvmti.so"
	# JVMTI is a build option, skip the test if fail to find lib
	exit 2
fi
