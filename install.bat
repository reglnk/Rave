@echo off
Powershell.exe -executionpolicy remotesigned -File ./llvm-patch.ps1

choco install make mingw wget

wget --tries=inf https://github.com/terralang/llvm-build/releases/download/llvm-16.0.3/clang+llvm-16.0.3-x86_64-windows-msvc17.7z

if not exist "LLVM" mkdir LLVM-15
if not exist "LLVM/bin" xcopy /s clang+llvm-16.0.3-x86_64-windows-msvc17.7z LLVM

if exist "clang+llvm-16.0.3-x86_64-windows-msvc17" rd /s /q "clang+llvm-16.0.3-x86_64-windows-msvc17"
if exist "clang+llvm-16.0.3-x86_64-windows-msvc17.7z" del "clang+llvm-16.0.3-x86_64-windows-msvc17.7z"

pause