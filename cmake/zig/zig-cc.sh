#!/bin/sh
exec "${ZIG:-zig}" cc -target "${ZIG_TARGET:-x86_64-linux-gnu.2.27}" "$@"
