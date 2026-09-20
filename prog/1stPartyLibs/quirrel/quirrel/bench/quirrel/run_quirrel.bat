echo off
@rem use the release sq built from this repo (jam -sConfig=rel in prog/tools/sq)
set SQ=%~dp0..\..\..\..\..\..\tools\util\sq-64.exe
%SQ% version.nut
%SQ% fib_recursive.nut
%SQ% fib_loop.nut
%SQ% primes.nut
%SQ% particles.nut
%SQ% dict.nut
%SQ% exp.nut
%SQ% nbodies.nut
%SQ% f2i.nut
%SQ% f2s.nut
%SQ% queen.nut
%SQ% spectral-norm.nut
%SQ% table-sort.nut
