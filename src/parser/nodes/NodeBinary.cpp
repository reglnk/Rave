/*
This Source Code Form is subject to the terms of the Mozilla
Public License, v. 2.0. If a copy of the MPL was not distributed
with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include "../../include/parser/ast.hpp"
#include "../../include/parser/Types.hpp"
#include <llvm-c/Target.h>
#include "../../include/parser/nodes/NodeBinary.hpp"
#include "../../include/lexer/tokens.hpp"
#include "../../include/parser/nodes/NodeIden.hpp"
#include "../../include/parser/nodes/NodeInt.hpp"
#include "../../include/parser/nodes/NodeFloat.hpp"
#include "../../include/parser/nodes/NodeString.hpp"
#include "../../include/parser/nodes/NodeVar.hpp"
#include "../../include/parser/nodes/NodeNull.hpp"
#include "../../include/parser/nodes/NodeGet.hpp"
#include "../../include/parser/nodes/NodeIndex.hpp"
#include "../../include/parser/nodes/NodeBuiltin.hpp"
#include "../../include/parser/nodes/NodeCast.hpp"
#include "../../include/parser/nodes/NodeBool.hpp"
#include "../../include/parser/nodes/NodeDone.hpp"
#include "../../include/parser/nodes/NodeStruct.hpp"
#include "../../include/parser/nodes/NodeCall.hpp"
#include "../../include/parser/nodes/NodeFunc.hpp"
#include "../../include/parser/nodes/NodeUnary.hpp"
#include "../../include/parser/nodes/NodeType.hpp"
#include "../../include/llvm.hpp"
#include "../../include/compiler.hpp"
#include <iostream>

LLVMValueRef Binary::castValue(LLVMValueRef from, LLVMTypeRef to, int loc) {
    if(LLVMTypeOf(from) == to) return from;
    LLVMTypeKind kindFrom = LLVMGetTypeKind(LLVMTypeOf(from));
    LLVMTypeKind kindTo = LLVMGetTypeKind(to);
    if(kindFrom == kindTo) {
        switch(kindFrom) {
            case LLVMIntegerTypeKind: return LLVMBuildIntCast(generator->builder, from, to, "castValueItoI");
            case LLVMFloatTypeKind: case LLVMDoubleTypeKind: case LLVMHalfTypeKind: case LLVMBFloatTypeKind: case LLVMFP128TypeKind: return LLVMBuildFPCast(generator->builder, from, to, "castValueFtoF");
            case LLVMPointerTypeKind: return LLVMBuildPointerCast(generator->builder, from, to, "castValuePtoP");
            case LLVMArrayTypeKind: generator->error("the ability to use cast to arrays is temporarily impossible!", loc); return nullptr;
            default: return from;
        }
    }
    switch(kindFrom) {
        case LLVMIntegerTypeKind:
            switch(kindTo) {
                case LLVMFloatTypeKind: case LLVMDoubleTypeKind: case LLVMHalfTypeKind: case LLVMBFloatTypeKind: case LLVMFP128TypeKind: return LLVMBuildSIToFP(generator->builder, from, to, "castValueItoF");
                case LLVMPointerTypeKind: return LLVMBuildIntToPtr(generator->builder, from, to, "castValueItoP");
                case LLVMArrayTypeKind: generator->error("it is forbidden to cast numbers into arrays!", loc); return nullptr;
                case LLVMStructTypeKind: generator->error("it is forbidden to cast numbers into structures!", loc); return nullptr;
                default: return from;
            } break;
        case LLVMFloatTypeKind: case LLVMDoubleTypeKind:
            switch(kindTo) {
                case LLVMIntegerTypeKind: return LLVMBuildFPToSI(generator->builder, from, to, "castValueFtoI");
                case LLVMHalfTypeKind: case LLVMBFloatTypeKind: case LLVMDoubleTypeKind: case LLVMFloatTypeKind: case LLVMFP128TypeKind: return LLVMBuildFPCast(generator->builder, from, to, "castValueFtoH");
                case LLVMPointerTypeKind: generator->error("it is forbidden to cast floating-point numbers into pointers!", loc); return nullptr;
                case LLVMArrayTypeKind: generator->error("it is forbidden to cast numbers into arrays!", loc); return nullptr;
                case LLVMStructTypeKind: generator->error("it is forbidden to cast numbers into structures!", loc); return nullptr;
                default: return from;
            }
        case LLVMPointerTypeKind:
            switch(kindTo) {
                case LLVMIntegerTypeKind: return LLVMBuildPtrToInt(generator->builder, from, to, "castValuePtoI");
                case LLVMFloatTypeKind: case LLVMDoubleTypeKind: case LLVMHalfTypeKind: case LLVMBFloatTypeKind: case LLVMFP128TypeKind:
                    if(std::string(LLVMPrintValueToString(from)).find("null") != std::string::npos) return LLVMConstReal(to, 0.0);
                    generator->error("it is forbidden to cast pointers into floating-point numbers!", loc); return nullptr;
                case LLVMArrayTypeKind:
                    if(std::string(LLVMPrintValueToString(from)).find("null") != std::string::npos) return LLVMConstNull(to);
                    generator->error("it is forbidden to cast pointers into arrays!", loc); return nullptr;
                case LLVMStructTypeKind:
                    if(std::string(LLVMPrintValueToString(from)).find("null") != std::string::npos) return LLVMConstNull(to);
                    #if RAVE_OPAQUE_POINTERS
                    return LLVMBuildLoad2(generator->builder, to, from, "castValueStoP");
                    #else
                    generator->error("it is forbidden to cast pointers into structures!", loc);
                    return nullptr;
                    #endif
                default: return from;
            }
        case LLVMArrayTypeKind:
            if(std::string(LLVMPrintValueToString(from)).find("null") != std::string::npos) return LLVMConstNull(to);
            generator->error("it is forbidden to casting arrays!", loc); return nullptr;
        case LLVMStructTypeKind: generator->error("it is forbidden to cast structures!", loc); return nullptr;
        default: return from;
    } return from;
}

LLVMValueRef Binary::sum(LLVMValueRef one, LLVMValueRef two, int loc) {
    LLVMValueRef oneCasted = one;
    LLVMValueRef twoCasted = two;

    if(LLVMGetTypeKind(LLVMTypeOf(one)) == LLVMGetTypeKind(LLVMTypeOf(two))) {
        if(LLVMTypeOf(one) != LLVMTypeOf(two)) oneCasted = Binary::castValue(one, LLVMTypeOf(two), loc); twoCasted = two;
    }
    else {oneCasted = Binary::castValue(one, LLVMTypeOf(two), loc); twoCasted = two;}

    if(
        LLVMGetTypeKind(LLVMTypeOf(oneCasted)) == LLVMIntegerTypeKind ||
        (
            LLVMGetTypeKind(LLVMTypeOf(oneCasted)) == LLVMVectorTypeKind &&
            LLVMGetTypeKind(LLVMGetElementType(LLVMTypeOf(oneCasted))) == LLVMIntegerTypeKind
        )
    ) return LLVMBuildAdd(generator->builder, one, two, "sum");
    return LLVMBuildFAdd(generator->builder, one, two, "fsum");
}

LLVMValueRef Binary::sub(LLVMValueRef one, LLVMValueRef two, int loc) {
    LLVMValueRef oneCasted = one;
    LLVMValueRef twoCasted = two;

    if(LLVMGetTypeKind(LLVMTypeOf(one)) == LLVMGetTypeKind(LLVMTypeOf(two))) {
        if(LLVMTypeOf(one) != LLVMTypeOf(two)) oneCasted = Binary::castValue(one, LLVMTypeOf(two), loc); twoCasted = two;
    }
    else {oneCasted = Binary::castValue(one, LLVMTypeOf(two), loc); twoCasted = two;}

    if(LLVMGetTypeKind(LLVMTypeOf(oneCasted)) == LLVMIntegerTypeKind) return LLVMBuildSub(generator->builder, one, two, "sub");
    return LLVMBuildFSub(generator->builder, one, two, "fsub");
}

LLVMValueRef Binary::mul(LLVMValueRef one, LLVMValueRef two, int loc) {
    LLVMValueRef oneCasted = one;
    LLVMValueRef twoCasted = two;

    if(LLVMGetTypeKind(LLVMTypeOf(one)) == LLVMGetTypeKind(LLVMTypeOf(two))) {
        if(LLVMTypeOf(one) != LLVMTypeOf(two)) oneCasted = Binary::castValue(one, LLVMTypeOf(two), loc); twoCasted = two;
    }
    else {oneCasted = Binary::castValue(one, LLVMTypeOf(two), loc); twoCasted = two;}

    if(LLVMGetTypeKind(LLVMTypeOf(oneCasted)) == LLVMIntegerTypeKind) return LLVMBuildMul(generator->builder, one, two, "sum");
    return LLVMBuildFMul(generator->builder, one, two, "fmul");
}

LLVMValueRef Binary::div(LLVMValueRef one, LLVMValueRef two, int loc) {
    LLVMValueRef oneCasted = one;
    LLVMValueRef twoCasted = two;

    if(LLVMGetTypeKind(LLVMTypeOf(one)) == LLVMGetTypeKind(LLVMTypeOf(two))) {
        if(LLVMTypeOf(one) != LLVMTypeOf(two)) {
            oneCasted = Binary::castValue(one, LLVMTypeOf(two), loc);
            twoCasted = two;
        }
    }
    else {oneCasted = Binary::castValue(one, LLVMTypeOf(two), loc); twoCasted = two;}

    if(LLVMGetTypeKind(LLVMTypeOf(oneCasted)) == LLVMIntegerTypeKind) return LLVMBuildSDiv(generator->builder, one, two, "div");
    return LLVMBuildFDiv(generator->builder, one, two, "fdiv");
}

LLVMValueRef Binary::compare(LLVMValueRef one, LLVMValueRef two, char op, int loc) {
    LLVMValueRef oneCasted = one;
    LLVMValueRef twoCasted = two;

    if(LLVMGetTypeKind(LLVMTypeOf(one)) == LLVMGetTypeKind(LLVMTypeOf(two))) {
        if(LLVMTypeOf(one) != LLVMTypeOf(two)) {
            oneCasted = Binary::castValue(one, LLVMTypeOf(two), loc); twoCasted = two;
        }
    }
    else {oneCasted = Binary::castValue(one, LLVMTypeOf(two), loc); twoCasted = two;}

    if(LLVMGetTypeKind(LLVMTypeOf(oneCasted)) == LLVMPointerTypeKind) {
        oneCasted = LLVMBuildPtrToInt(generator->builder, oneCasted, LLVMInt64TypeInContext(generator->context), "comparePtoIOne");
        twoCasted = LLVMBuildPtrToInt(generator->builder, twoCasted, LLVMInt64TypeInContext(generator->context), "comparePtoITwo");
    }
    if(LLVMGetTypeKind(LLVMTypeOf(oneCasted)) == LLVMIntegerTypeKind) switch(op) {
        case TokType::Equal: return LLVMBuildICmp(generator->builder, LLVMIntEQ, one, two, "compareIEQ");
        case TokType::Nequal: return LLVMBuildICmp(generator->builder, LLVMIntNE, one, two, "compareINEQ");
        case TokType::More: return LLVMBuildICmp(generator->builder, LLVMIntSGT, one, two, "compareIMR");
        case TokType::Less: return LLVMBuildICmp(generator->builder, LLVMIntSLT, one, two, "compareILS");
        case TokType::MoreEqual: return LLVMBuildICmp(generator->builder, LLVMIntSGE, one, two, "compareIME");
        case TokType::LessEqual: return LLVMBuildICmp(generator->builder, LLVMIntSLE, one, two, "compareILE");
        default: return nullptr;
    }
    switch(op) {
        case TokType::Equal: return LLVMBuildFCmp(generator->builder, LLVMRealOEQ, one, two, "compareIEQ");
        case TokType::Nequal: return LLVMBuildFCmp(generator->builder, LLVMRealONE, one, two, "compareINEQ");
        case TokType::More: return LLVMBuildFCmp(generator->builder, LLVMRealOGT, one, two, "compareIMR");
        case TokType::Less: return LLVMBuildFCmp(generator->builder, LLVMRealOLT, one, two, "compareILS");
        case TokType::MoreEqual: return LLVMBuildFCmp(generator->builder, LLVMRealOGE, one, two, "IcompareME");
        case TokType::LessEqual: return LLVMBuildFCmp(generator->builder, LLVMRealOLE, one, two, "IcompareLE");
        default: return nullptr;
    } return nullptr;
}

void Binary::store(RaveValue pointer, RaveValue value, int loc) {
    if(pointer.type == nullptr || pointer.value == nullptr || value.type == nullptr || value.value == nullptr) return;

    Type* memType = pointer.type->getElType();

    if(instanceof<TypeBasic>(memType)) {
        if(instanceof<TypeBasic>(value.type)) {
            TypeBasic* memBasic = (TypeBasic*)memType;
            TypeBasic* valBasic = (TypeBasic*)value.type;

            if(memBasic->type != valBasic->type) {
                value.type = memBasic;
                value.value = Binary::castValue(value.value, generator->genType(memBasic, loc), loc);
            }
        }
        else if(!instanceof<TypeStruct>(value.type)) {
            generator->error("cannot store a value of type " + value.type->toString() + " into a value of type " + memType->toString() + "!", loc);
            return;
        }
    }
    else if(instanceof<TypePointer>(memType)) {
        if(instanceof<TypeBasic>(value.type)) {
            generator->error("cannot store a value of type " + value.type->toString() + " into a value of type " + memType->toString() + "!", loc);
            return;
        }
    }

    LLVMBuildStore(generator->builder, value.value, pointer.value);
}

NodeBinary::NodeBinary(char op, Node* first, Node* second, int loc, bool isStatic) {
    this->op = op;
    this->first = first;
    this->second = second;
    this->loc = loc;
    this->isStatic = isStatic;
}

std::pair<std::string, std::string> NodeBinary::isOperatorOverload(RaveValue first, RaveValue second, char op) {
    if(first.value == nullptr || second.value == nullptr) return {"", ""};

    Type* type = first.type;

    if(instanceof<TypeStruct>(type) || (instanceof<TypePointer>(type) && instanceof<TypeStruct>(((TypePointer*)type)->instance))) {
        std::string structName = (instanceof<TypeStruct>(type) ? ((TypeStruct*)type)->name : ((TypeStruct*)(((TypePointer*)type)->instance))->name);
        if(AST::structTable.find(structName) != AST::structTable.end()) {
            auto& operators = AST::structTable[structName]->operators;
            if(operators.find(op) != operators.end()) {
                std::vector<Type*> types = {this->first->getType(), this->second->getType()};
                std::string sTypes = typesToString(types);
                if(operators[op].find(sTypes) != operators[op].end()) return {structName, sTypes};
            }
            else if(op == TokType::Nequal && operators.find(TokType::Equal) != operators.end()) {
                std::vector<Type*> types = {this->first->getType(), this->second->getType()};
                std::string sTypes = typesToString(types);
                if(operators[TokType::Equal].find(sTypes) != operators[TokType::Equal].end()) return {"!" + structName, sTypes};
            }
        }
    }
    return {"", ""};
}

Node* NodeBinary::copy() {return new NodeBinary(this->op, this->first->copy(), this->second->copy(), this->loc, this->isStatic);}
void NodeBinary::check() {this->isChecked = true;}

Node* NodeBinary::comptime() {
    Node* first = this->first;
    Node* second = this->second;

    while(instanceof<NodeIden>(first) && AST::aliasTable.find(((NodeIden*)first)->name) != AST::aliasTable.end()) first = AST::aliasTable[((NodeIden*)first)->name];
    while(instanceof<NodeIden>(second) && AST::aliasTable.find(((NodeIden*)second)->name) != AST::aliasTable.end()) second = AST::aliasTable[((NodeIden*)second)->name];
    
    while(!instanceof<NodeType>(first) && !instanceof<NodeBool>(first) && !instanceof<NodeString>(first) && !instanceof<NodeIden>(first) && !instanceof<NodeInt>(first) && !instanceof<NodeFloat>(first)) first = first->comptime();
    while(!instanceof<NodeType>(second) && !instanceof<NodeBool>(second) && !instanceof<NodeString>(second) && !instanceof<NodeIden>(second) && !instanceof<NodeInt>(second) && !instanceof<NodeFloat>(second)) second = second->comptime();

    if(instanceof<NodeIden>(first) || instanceof<NodeIden>(second) || instanceof<NodeType>(first) || instanceof<NodeType>(second)) {        
        Type* firstType = (instanceof<NodeIden>(first) ? new TypeStruct(((NodeIden*)first)->name) : ((NodeType*)first)->type);
        Type* secondType = (instanceof<NodeIden>(second) ? new TypeStruct(((NodeIden*)second)->name) : ((NodeType*)second)->type);

        if(generator != nullptr) {
            while(generator->toReplace.find(firstType->toString()) != generator->toReplace.end()) firstType = generator->toReplace[firstType->toString()];
            while(generator->toReplace.find(secondType->toString()) != generator->toReplace.end()) secondType = generator->toReplace[secondType->toString()];
        }

        switch(this->op) {
            case TokType::Equal: return new NodeBool(firstType->toString() == secondType->toString());
            case TokType::Nequal: return new NodeBool(firstType->toString() != secondType->toString());
            default: return new NodeBool(false);
        }
    }

    NodeBool* eqNeqResult = nullptr;

    switch(this->op) {
        case TokType::Equal: case TokType::Nequal:
            eqNeqResult = new NodeBool(true);

            if(instanceof<NodeString>(first) && instanceof<NodeString>(second)) eqNeqResult->value = ((NodeString*)first)->value == ((NodeString*)second)->value;
            else if(instanceof<NodeBool>(first)) {
                if(instanceof<NodeBool>(second)) eqNeqResult->value = ((NodeBool*)first)->value == ((NodeBool*)second)->value;
                else if(instanceof<NodeInt>(second)) eqNeqResult->value = ((NodeInt*)second)->value == (((NodeBool*)first)->value ? "1" : "0");
                else if(instanceof<NodeFloat>(second)) {
                    std::string _float = ((NodeFloat*)second)->value;
                    int dot = _float.find('.');

                    if(dot != std::string::npos) {
                        while(_float.back() == '0') _float.pop_back();
                        if(_float.back() == '.') _float.pop_back();
                    }

                    eqNeqResult->value = _float == (((NodeBool*)first)->value ? "1" : "0");
                }
            }
            else if(instanceof<NodeInt>(first)) {
                if(instanceof<NodeInt>(second)) eqNeqResult->value = ((NodeInt*)first)->value == ((NodeInt*)second)->value;
                else if(instanceof<NodeBool>(second)) eqNeqResult->value = ((NodeInt*)first)->value == (((NodeBool*)second)->value ? "1" : "0");
                else if(instanceof<NodeFloat>(second)) {
                    std::string _float = ((NodeFloat*)second)->value;
                    int dot = _float.find('.');

                    if(dot != std::string::npos) {
                        while(_float.back() == '0') _float.pop_back();
                        if(_float.back() == '.') _float.pop_back();
                    }

                    eqNeqResult->value = _float == ((NodeInt*)first)->value.to_string();
                }
            }
            else if(instanceof<NodeFloat>(first)) {
                std::string _float = ((NodeFloat*)first)->value;
                int dot = _float.find('.');
                if(dot != std::string::npos) {
                    while(_float.back() == '0') _float.pop_back();
                    if(_float.back() == '.') _float.pop_back();
                }
                
                if(instanceof<NodeFloat>(second)) {
                    std::string _float2 = ((NodeFloat*)second)->value;
                    int dot2 = _float2.find('.');
                    if(dot2 != std::string::npos) {
                        while(_float2.back() == '0') _float2.pop_back();
                        if(_float2.back() == '.') _float2.pop_back();
                    }
                    eqNeqResult->value = _float == _float2;
                }
                else if(instanceof<NodeInt>(second)) eqNeqResult->value = _float == ((NodeInt*)second)->value.to_string();
                else if(instanceof<NodeBool>(second)) eqNeqResult->value = _float == (((NodeBool*)second)->value ? "1" : "0");
            }
            
            if(this->op == TokType::Nequal) eqNeqResult->value = !eqNeqResult->value;

            return eqNeqResult;
        case TokType::And: return new NodeBool(((NodeBool*)first->comptime())->value && ((NodeBool*)second->comptime())->value);
        case TokType::Or: return new NodeBool(((NodeBool*)first->comptime())->value || ((NodeBool*)second->comptime())->value);
        case TokType::More: case TokType::Less: case TokType::MoreEqual: case TokType::LessEqual:
            if(instanceof<NodeInt>(first)) {
                if(instanceof<NodeInt>(second)) {
                    if(this->op == TokType::More) return new NodeBool(((NodeInt*)first)->value > ((NodeInt*)second)->value);
                    else if(this->op == TokType::Less) return new NodeBool(((NodeInt*)first)->value < ((NodeInt*)second)->value);
                    else if(this->op == TokType::MoreEqual) return new NodeBool(((NodeInt*)first)->value >= ((NodeInt*)second)->value);
                    else return new NodeBool(((NodeInt*)first)->value <= ((NodeInt*)second)->value);
                }
                else if(instanceof<NodeFloat>(second)) {
                    if(((NodeInt*)first)->type == BasicType::Cent || ((NodeInt*)first)->type == BasicType::Ucent) return new NodeBool(false); // TODO: is this a error?
                    R128 r128;
                    r128FromString(&r128, ((NodeFloat*)second)->value.c_str(), nullptr);
                    if(this->op == TokType::More) return new NodeBool(r128 > R128(((NodeInt*)first)->value.to_long_long()));
                    else if(this->op == TokType::Less) return new NodeBool(r128 < R128(((NodeInt*)first)->value.to_long_long()));
                    else if(this->op == TokType::MoreEqual) return new NodeBool(r128 >= R128(((NodeInt*)first)->value.to_long_long()));
                    else return new NodeBool(r128 <= R128(((NodeInt*)first)->value.to_long_long()));
                }
                else if(instanceof<NodeBool>(second)) return new NodeBool(((NodeInt*)first)->value > (((NodeBool*)second)->value ? 1 : 0));
                else return new NodeBool(false);
            }
            else if(instanceof<NodeFloat>(first)) {
                if(instanceof<NodeFloat>(second)) {
                    R128 r128_1, r128_2;
                    r128FromString(&r128_1, ((NodeFloat*)first)->value.c_str(), nullptr);
                    r128FromString(&r128_2, ((NodeFloat*)first)->value.c_str(), nullptr);
                    if(this->op == TokType::More) return new NodeBool(r128_1 > r128_2);
                    else if(this->op == TokType::Less) return new NodeBool(r128_1 < r128_2);
                    else if(this->op == TokType::MoreEqual) return new NodeBool(r128_1 >= r128_2);
                    else return new NodeBool(r128_1 <= r128_2);
                }
                else if(instanceof<NodeInt>(second)) {
                    if(((NodeInt*)second)->type == BasicType::Cent || ((NodeInt*)second)->type == BasicType::Ucent) return new NodeBool(false); // TODO: is this a error?
                    R128 r128;
                    r128FromString(&r128, ((NodeFloat*)first)->value.c_str(), nullptr);
                    if(this->op == TokType::More) return new NodeBool(r128 > R128(((NodeInt*)second)->value.to_long_long()));
                    else if(this->op == TokType::Less) return new NodeBool(r128 < R128(((NodeInt*)second)->value.to_long_long()));
                    else if(this->op == TokType::MoreEqual) return new NodeBool(r128 >= R128(((NodeInt*)second)->value.to_long_long()));
                    else return new NodeBool(r128 <= R128(((NodeInt*)second)->value.to_long_long()));
                }
                // TODO: add bool
            }
            return new NodeBool(false);
        default: return new NodeBool(false);
    }
}

Type* NodeBinary::getType() {
    switch(this->op) {
        case TokType::Equ: case TokType::PluEqu: case TokType::MinEqu: case TokType::DivEqu: case TokType::MulEqu: return typeVoid;
        case TokType::Equal: case TokType::Nequal: case TokType::More: case TokType::Less: case TokType::MoreEqual: case TokType::LessEqual:
        case TokType::And: case TokType::Or: return basicTypes[BasicType::Bool];
        default:
            Type* firstType = this->first->getType();
            Type* secondType = this->second->getType();
            if(firstType == nullptr) return secondType;
            if(secondType == nullptr) return firstType;
            return (firstType->getSize() >= secondType->getSize()) ? firstType : secondType;
    }
    return nullptr;
}

RaveValue NodeBinary::generate() {
    bool isAliasIden = (this->first != nullptr && instanceof<NodeIden>(this->first) && AST::aliasTable.find(((NodeIden*)this->first)->name) != AST::aliasTable.end());

    if(this->op == TokType::PluEqu || this->op == TokType::MinEqu || this->op == TokType::MulEqu || this->op == TokType::DivEqu) {
        RaveValue value;
        NodeBinary* bin = new NodeBinary(TokType::Equ, this->first->copy(), new NodeBinary((
            (this->op == TokType::PluEqu ? TokType::Plus : (this->op == TokType::MinEqu ? TokType::Minus : (this->op == TokType::MulEqu ? TokType::Multiply : TokType::Divide)))
        ), this->first->copy(), this->second->copy(), loc), loc);
        bin->check();
        value = bin->generate();
        return value;
    }

    if(this->op == TokType::Equ) {
        if((instanceof<NodeCast>(second)) && instanceof<TypeVoid>(second->getType())) {
            generator->error("an attempt to store a void to the variable!", this->loc);
            return {};
        }

        if(instanceof<NodeIden>(first)) {
            NodeIden* id = ((NodeIden*)first);

            if(isAliasIden) {AST::aliasTable[id->name] = this->second->copy(); return {};}

            if(currScope->locatedAtThis(id->name)) {
                this->first = new NodeGet(new NodeIden("this", this->loc), id->name, true, id->loc);
                return this->generate();
            }

            if(currScope->getVar(id->name, this->loc)->isConst && id->name != "this" && currScope->getVar(id->name, this->loc)->isChanged) {
                generator->error("an attempt to change the value of a constant variable!", this->loc);
                return {};
            }

            if(!currScope->getVar(id->name, this->loc)->isChanged) currScope->hasChanged(id->name);

            if(currScope->localVars.find(id->name) != currScope->localVars.end()) currScope->localVars[id->name]->isUsed = true;

            NodeVar* nvar = currScope->getVar(id->name, this->loc);

            RaveValue vSecond = {nullptr, nullptr};
            
            if(instanceof<TypeStruct>(nvar->type) || (instanceof<TypePointer>(nvar->type) && instanceof<TypeStruct>(((TypePointer*)nvar->type)->instance))
               &&!instanceof<TypeStruct>(this->second->getType()) || (instanceof<TypePointer>(this->second->getType()) && instanceof<TypeStruct>(((TypePointer*)this->second->getType())->instance))
            ) {
                RaveValue vFirst = first->generate();
                vSecond = second->generate();

                std::pair<std::string, std::string> opOverload = isOperatorOverload(vFirst, vSecond, this->op);

                if(opOverload.first != "") {
                    if(opOverload.first[0] == '!') return (new NodeUnary(this->loc, TokType::Ne, (new NodeCall(
                        this->loc, new NodeIden(AST::structTable[opOverload.first.substr(1)]->operators[this->op][opOverload.second]->name, this->loc),
                        std::vector<Node*>({new NodeDone(vFirst), new NodeDone(vSecond)})))))->generate();
                    return (new NodeCall(
                        this->loc, new NodeIden(AST::structTable[opOverload.first]->operators[this->op][opOverload.second]->name, this->loc),
                        std::vector<Node*>({new NodeDone(vFirst), new NodeDone(vSecond)})))->generate();
                }
                else if(instanceof<TypeBasic>(this->second->getType())) {
                    generator->error("an attempt to change value of the structure as the variable '" + id->name + "' without overloading!", this->loc);
                    return {};
                }
            }
            else vSecond = second->generate();

            RaveValue value = currScope->getWithoutLoad(id->name, this->loc);

            if(vSecond.type && vSecond.type->toString() == value.type->toString()) vSecond = LLVM::load(vSecond, "NodeBinary_NodeIden_load", loc);

            if(instanceof<NodeNull>(this->second)) {
                ((NodeNull*)(this->second))->type = value.type->getElType();
                vSecond = second->generate();
            }

            Binary::store(value, vSecond, loc);
            return {};
        }
        else if(instanceof<NodeGet>(this->first)) {
            NodeGet* nget = (NodeGet*)this->first;
            RaveValue fValue = nget->generate();

            if(instanceof<NodeNull>(this->second)) {
                ((NodeNull*)this->second)->type = fValue.type->getElType();
            }

            RaveValue sValue = this->second->generate();

            nget->isMustBePtr = true;
            fValue = nget->generate();

            if(nget->elementIsConst) {
                generator->error("An attempt to change the constant element!", loc);
                return {};
            }
 
            Binary::store(fValue, sValue, loc);
            return {};
        }
        else if(instanceof<NodeIndex>(this->first)) {
            NodeIndex* ind = (NodeIndex*)this->first;
            ind->isMustBePtr = true;

            if(instanceof<NodeIden>(ind->element) && currScope->locatedAtThis(((NodeIden*)ind->element)->name)) {
                ind->element = new NodeGet(new NodeIden("this", this->loc), ((NodeIden*)ind->element)->name, true, ((NodeIden*)ind->element)->loc);
                return this->generate();
            }

            RaveValue value = {nullptr, nullptr};

            RaveValue ptr = ind->generate();
            if(ind->elementIsConst) generator->error("An attempt to change the constant element!", loc);

            // Check for TypeVector
            if(instanceof<TypeVector>(ind->element->getType())) {
                Type* elType = ((TypeVector*)ind->element->getType())->getElType();
                RaveValue number = this->second->generate();

                if(instanceof<TypeBasic>(number.type) && ((TypeBasic*)number.type)->type != ((TypeBasic*)elType)->type) {
                    number.value = Binary::castValue(number.value, generator->genType(elType, loc), loc);
                    number.type = elType;
                }

                if(number.type->toString() != elType->toString()) generator->error("cannot store a value of type " + number.type->toString() + " into a value of type " + elType->toString() + "!", loc);

                if(instanceof<TypePointer>(ptr.type)) {
                    value = LLVM::load(ptr, "NodeBinary_TypeVector_load", loc);
                    value.value = LLVMBuildInsertElement(generator->builder, value.value, number.value, ind->indexes[0]->generate().value, "NodeBinary_TypeVector_insert");
                }
                else value = {LLVMBuildInsertElement(generator->builder, value.value, number.value, ind->indexes[0]->generate().value, "NodeBinary_TypeVector_insert"), ptr.type};
            }
            else value = this->second->generate();

            if(instanceof<NodeNull>(this->second)) {
                ((NodeNull*)this->second)->type = ptr.type->getElType();
            }
            
            Binary::store(ptr, value, loc);
            return {};
        }
        else if(instanceof<NodeDone>(this->first)) {
            Binary::store(this->first->generate(), this->second->generate(), loc);
            return {};
        }
    }

    if(instanceof<NodeNull>(this->first)) ((NodeNull*)this->first)->type = this->second->getType();
    else if(instanceof<NodeNull>(this->second)) ((NodeNull*)this->second)->type = this->first->getType();   

    if(this->op == TokType::Rem) {
        Type* type = this->first->getType();
        if(instanceof<TypeBasic>(type) && (((TypeBasic*)type)->type == BasicType::Float || ((TypeBasic*)type)->type == BasicType::Double)) return (new NodeBuiltin("fmodf", {this->first, this->second}, this->loc, nullptr))->generate();
        return (new NodeCast(instanceof<TypeVoid>(type) ? basicTypes[BasicType::Double] : type, new NodeBuiltin("fmodf", {this->first, this->second}, this->loc, nullptr), this->loc))->generate();
    }

    RaveValue vFirst = this->first->generate();
    RaveValue vSecond = this->second->generate();

    while(instanceof<TypeConst>(vFirst.type)) vFirst.type = vFirst.type->getElType();
    while(instanceof<TypeConst>(vSecond.type)) vSecond.type = vSecond.type->getElType();

    if(instanceof<TypeStruct>(this->first->getType()) || instanceof<TypePointer>(this->first->getType())
     && !instanceof<TypeStruct>(this->second->getType()) && !instanceof<TypePointer>(this->second->getType())) {
        std::pair<std::string, std::string> opOverload = isOperatorOverload(vFirst, vSecond, this->op);
        if(opOverload.first != "") {
            if(opOverload.first[0] == '!') return (new NodeUnary(loc, TokType::Ne, (new NodeCall(
                this->loc, new NodeIden(AST::structTable[opOverload.first.substr(1)]->operators[TokType::Equal][opOverload.second]->name, this->loc),
                std::vector<Node*>({first, second})))))->generate();
            return (new NodeCall(
                this->loc, new NodeIden(AST::structTable[opOverload.first]->operators[this->op][opOverload.second]->name, this->loc),
                std::vector<Node*>({first, second})))->generate();
        }
    }

    if(instanceof<TypePointer>(vFirst.type) && !instanceof<TypePointer>(vSecond.type)) vFirst = LLVM::load(vFirst, "NodeBinary_loadF", loc);
    else if(instanceof<TypePointer>(vSecond.type) && !instanceof<TypePointer>(vFirst.type)) vSecond = LLVM::load(vSecond, "NodeBinary_loadS", loc);

    if(generator->toReplace.find(vFirst.type->toString()) != generator->toReplace.end()) vFirst.type = generator->toReplace[vFirst.type->toString()];

    if(instanceof<TypeBasic>(vFirst.type) && instanceof<TypeBasic>(vSecond.type) && vFirst.type->toString() != vSecond.type->toString()) {
        TypeBasic* t1 = (TypeBasic*)vFirst.type;
        TypeBasic* t2 = (TypeBasic*)vSecond.type;

        if(t1->isFloat()) {
            if(t2->isFloat()) {
                if(t1->getSize() > t2->getSize()) {
                    vSecond.value = LLVMBuildFPCast(generator->builder, vSecond.value, generator->genType(t1, loc), "NodeBinary_ftof");
                    vSecond.type = t1;
                }
                else {
                    vFirst.value = LLVMBuildFPCast(generator->builder, vFirst.value, generator->genType(t2, loc), "NodeBinary_ftof");
                    vFirst.type = t2;
                }
            }
            else {
                vSecond.value = LLVMBuildSIToFP(generator->builder, vSecond.value, generator->genType(t1, loc), "NodeBinary_itof");
                vSecond.type = t1;
            }
        }
        else if(t2->isFloat()) {
            vFirst.value = LLVMBuildSIToFP(generator->builder, vFirst.value, generator->genType(t2, loc), "NodeBinary_itof");
            vFirst.type = t2;
        }
        else if(t1->getSize() > t2->getSize()) {
            vSecond.value = LLVMBuildIntCast2(generator->builder, vSecond.value, generator->genType(t1, loc), false, "NodeBinary_itoi");
            vSecond.type = t1;
        }
        else {
            vFirst.value = LLVMBuildIntCast2(generator->builder, vFirst.value, generator->genType(t2, loc), false, "NodeBinary_itoi");
            vFirst.type = t2;
        }
    }

    if(vFirst.type->toString() != vSecond.type->toString()) generator->error("value types '" + vFirst.type->toString() + "' and '" + vSecond.type->toString() + "' are incompatible!", loc);
    
    switch(this->op) {
        case TokType::Plus: return {Binary::sum(vFirst.value, vSecond.value, this->loc), vFirst.type};
        case TokType::Minus: return {Binary::sub(vFirst.value, vSecond.value, this->loc), vFirst.type};
        case TokType::Multiply: return {Binary::mul(vFirst.value, vSecond.value, this->loc), vFirst.type};
        case TokType::Divide: return {Binary::div(vFirst.value, vSecond.value, this->loc), vFirst.type};
        case TokType::Equal: case TokType::Nequal:
        case TokType::Less: case TokType::More:
        case TokType::LessEqual: case TokType::MoreEqual: return {Binary::compare(vFirst.value, vSecond.value, this->op, this->loc), basicTypes[BasicType::Bool]};
        case TokType::And: return {LLVMBuildAnd(generator->builder, vFirst.value, vSecond.value, "NodeBinary_and"), vFirst.type};
        case TokType::Or: return {LLVMBuildOr(generator->builder, vFirst.value, vSecond.value, "NodeBinary_or"), vFirst.type};
        case TokType::BitXor: return {LLVMBuildXor(generator->builder, vFirst.value, vSecond.value, "NodeBinary_xor"), vFirst.type};
        case TokType::BitLeft: return {LLVMBuildShl(generator->builder, vFirst.value, vSecond.value, "NodeBinary_and"), vFirst.type};
        case TokType::BitRight: return {LLVMBuildLShr(generator->builder, vFirst.value, vSecond.value, "NodeBinary_and"), vFirst.type};
        default: generator->error("undefined operator " + std::to_string(this->op) + "!", this->loc); return {};
    }
}

NodeBinary::~NodeBinary() {
    if(this->first != nullptr) delete this->first;
    if(this->second != nullptr) delete this->second;
}