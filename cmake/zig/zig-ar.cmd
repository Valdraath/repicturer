@echo off
if "%ZIG%"=="" set ZIG=zig
"%ZIG%" ar %*
