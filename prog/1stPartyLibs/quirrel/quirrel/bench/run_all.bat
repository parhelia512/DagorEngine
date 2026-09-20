echo off
@rem the release sq built from this repo (jam -sConfig=rel in prog/tools/sq)
set SQ=%~dp0..\..\..\..\..\tools\util\sq-64.exe
pushd quirrel
echo "Quirrel latest"
%SQ% -v
%SQ% version.nut
echo "----"
%SQ% fib_loop.nut
%SQ% fib_recursive.nut
%SQ% primes.nut
%SQ% particles.nut
%SQ% dict.nut
%SQ% exp.nut
%SQ% nbodies.nut
rem %SQ% native.nut
rem %SQ% profile_try_catch.nut 
popd

pushd lua
echo ""echo "----"
echo "LuaJIT2.1 -joff"
luajit.exe -joff fib_loop.lua
luajit.exe -joff fib_recursive.lua
luajit.exe -joff primes.lua
luajit.exe -joff particles.lua
luajit.exe -joff dict.lua 
rem luajit.exe -joff profile_try_catch.lua 
luajit.exe -joff exp.lua 
luajit.exe -joff nbodies.lua

echo "----"
echo "Lua 5.5.1"
lua.exe fib_loop.lua
lua.exe fib_recursive.lua
lua.exe primes.lua
lua.exe particles.lua
lua.exe dict.lua 
rem lua.exe profile_try_catch.lua 
lua.exe exp.lua 
lua.exe nbodies.lua
popd

pushd js
echo "----"
echo "QuickJS"
qjs.exe fib_loop.js
qjs.exe fib_recursive.js
qjs.exe primes.js
qjs.exe particles.js
qjs.exe dict.js
qjs.exe exp.js
qjs.exe nbodies.js
popd