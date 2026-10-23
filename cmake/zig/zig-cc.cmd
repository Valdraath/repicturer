@echo off
rem zig cc wrapper for cmake (linux cross build from windows); ZIG / ZIG_TARGET may be overridden
if "%ZIG%"=="" set ZIG=zig
if "%ZIG_TARGET%"=="" set ZIG_TARGET=x86_64-linux-gnu.2.27
"%ZIG%" cc -target %ZIG_TARGET% %*
