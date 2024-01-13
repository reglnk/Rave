/*
This Source Code Form is subject to the terms of the Mozilla
Public License, v. 2.0. If a copy of the MPL was not distributed
with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include "./include/llvm.hpp"
#include "./include/llvm-c/Target.h"
#include "./include/parser/ast.hpp"
#include <iostream>

LLVMValueRef LLVM::load(LLVMValueRef value, const char* name) {
    #if LLVM_VERSION_MAJOR >= 15
        return LLVMBuildLoad2(generator->builder, LLVMGetElementType(LLVMTypeOf(value)), value, name);
    #else
        return LLVMBuildLoad(generator->builder, value, name);
    #endif
}

LLVMValueRef LLVM::call(LLVMValueRef fn, LLVMValueRef* args, unsigned int argsCount, const char* name) {
    #if LLVM_VERSION_MAJOR >= 15
        return LLVMBuildCall2(generator->builder, LLVMGetReturnType(LLVMTypeOf(fn)), fn, args, argsCount, name);
    #else
        return LLVMBuildCall(generator->builder, fn, args, argsCount, name);
    #endif
}

LLVMValueRef LLVM::gep(LLVMValueRef ptr, LLVMValueRef* indices, unsigned int indicesCount, const char* name) {
    #if LLVM_VERSION_MAJOR >= 15
        return LLVMBuildGEP2(generator->builder, LLVMGetElementType(LLVMTypeOf(ptr)), ptr, indices, indicesCount, name);
    #else
        return LLVMBuildGEP(ptr, indices, indicesCount, name);
    #endif
}

LLVMValueRef LLVM::structGep(LLVMValueRef ptr, unsigned int idx, const char* name) {
    #if LLVM_VERSION_MAJOR >= 15
        return LLVMBuildStructGEP2(
            generator->builder, LLVMGetElementType(LLVMTypeOf(ptr)),
            ptr, idx, name
        );
    #else
        return LLVMBuildStructGEP(generator->builder, ptr, idx, name);
    #endif
}