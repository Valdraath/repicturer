#!/bin/sh
exec "${ZIG:-zig}" c++ -target "${ZIG_TARGET:-x86_64-linux-gnu.2.27}" "$@"
