@echo off
if "%ZIG%"=="" set ZIG=zig
if "%ZIG_TARGET%"=="" set ZIG_TARGET=x86_64-linux-gnu.2.27
"%ZIG%" c++ -target %ZIG_TARGET% %*
