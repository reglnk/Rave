/*
This Source Code Form is subject to the terms of the Mozilla
Public License, v. 2.0. If a copy of the MPL was not distributed
with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include "../include/parser/ast.hpp"
#include "../include/parser/Types.hpp"
#include "../include/llvm-c/Target.h"
#include "../include/llvm-c/Analysis.h"
#include "../include/parser/nodes/NodeFunc.hpp"
#include "../include/parser/nodes/NodeVar.hpp"
#include "../include/parser/nodes/NodeRet.hpp"
#include "../include/parser/nodes/NodeLambda.hpp"
#include "../include/parser/nodes/NodeIden.hpp"
#include "../include/parser/nodes/NodeStruct.hpp"
#include "../include/parser/nodes/NodeCall.hpp"
#include "../include/parser/nodes/NodeType.hpp"
#include "../include/parser/nodes/NodeGet.hpp"
#include "../include/parser/nodes/NodeInt.hpp"
#include "../include/json.hpp"
#include "../include/compiler.hpp"
#include <iostream>
#include "../include/llvm.hpp"
#include "../include/utils.hpp"

std::map<std::string, Type*> AST::aliasTypes;
std::map<std::string, Node*> AST::aliasTable;
std::map<std::string, NodeVar*> AST::varTable;
std::map<std::string, NodeFunc*> AST::funcTable;
std::map<std::string, NodeLambda*> AST::lambdaTable;
std::map<std::string, NodeStruct*> AST::structTable;
std::map<std::pair<std::string, std::string>, NodeFunc*> AST::methodTable;
std::map<std::pair<std::string, std::string>, StructMember> AST::structsNumbers;
std::vector<std::string> AST::importedFiles;
std::map<std::string, std::vector<Node*>> AST::parsed;
std::string AST::mainFile;
std::vector<std::string> AST::addToImport;
bool AST::debugMode;

LLVMGen* generator = nullptr;
Scope* currScope;
LLVMTargetDataRef dataLayout;

TypeFunc* callToTFunc(NodeCall* call) {
    std::vector<TypeFuncArg*> argTypes;
    for(Node* nd: call->args) {
        if(instanceof<NodeCall>(nd)) argTypes.push_back(new TypeFuncArg(callToTFunc((NodeCall*)nd), ""));
        else if(instanceof<NodeIden>(nd)) argTypes.push_back(new TypeFuncArg(getType(((NodeIden*)nd)->name), ""));
        else if(instanceof<NodeType>(nd)) argTypes.push_back(new TypeFuncArg(((NodeType*)nd)->type, ""));
    }
    return new TypeFunc(getType(((NodeIden*)call->func)->name), argTypes, false);
}

std::vector<Type*> parametersToTypes(std::vector<RaveValue> params) {
    if(params.empty()) return {};
    std::vector<Type*> types;

    for(int i=0; i<params.size(); i++) types.push_back(params[i].type);
    return types;
}

std::string typeToString(Type* arg) {
    if(instanceof<TypeBasic>(arg)) {
        switch(((TypeBasic*)arg)->type) {
            case BasicType::Half: return "hf";
            case BasicType::Bhalf: return "bf";
            case BasicType::Float: return "f";
            case BasicType::Double: return "d";
            case BasicType::Int: return "i";
            case BasicType::Uint: return "ui";
            case BasicType::Cent: return "t";
            case BasicType::Ucent: return "ut";
            case BasicType::Char: return "c";
            case BasicType::Uchar: return "uc";
            case BasicType::Long: return "l";
            case BasicType::Ulong: return "ul";
            case BasicType::Short: return "h";
            case BasicType::Ushort: return "uh";
            case BasicType::Real: return "r";
            default: return "b";
        }
    }
    else if(instanceof<TypePointer>(arg)) {
        if(instanceof<TypeStruct>(((TypePointer*)arg)->instance)) {
            TypeStruct* ts = (TypeStruct*)(((TypePointer*)arg)->instance);
            Type* t = ts;
            while(generator->toReplace.find(t->toString()) != generator->toReplace.end()) t = generator->toReplace[t->toString()];
            if(!instanceof<TypeStruct>(t)) return typeToString(new TypePointer(t));
            ts = (TypeStruct*)t;
            if(ts->name.find('<') == std::string::npos) return "s-" + ts->name;
            else return "s-" + ts->name.substr(0, ts->name.find('<'));
        }
        else {
            std::string buffer = "p";
            Type* element = ((TypePointer*)arg)->instance;

            while(!instanceof<TypeBasic>(element) && !instanceof<TypeVoid>(element) && !instanceof<TypeFunc>(element) && !instanceof<TypeStruct>(element)) {
                if(instanceof<TypeConst>(element)) {element = ((TypeConst*)element)->instance; continue;}

                if(instanceof<TypeArray>(element)) {
                    TypeArray* tarray = (TypeArray*)element;
                    if(!instanceof<TypeBasic>(tarray->element) && !instanceof<TypeVoid>(tarray->element) &&
                       !instanceof<TypeFunc>(tarray->element) && !instanceof<TypeStruct>(tarray->element)) buffer += "a";
                    element = ((TypeArray*)element)->element;
                }
                else if(instanceof<TypePointer>(element)) {
                    buffer += "p";
                    element = ((TypePointer*)element)->instance;
                }
            }

            buffer += (instanceof<TypeVoid>(element) ? "c" : typeToString(element));
            return buffer;
        }
    }
    else if(instanceof<TypeArray>(arg)) {
        std::string buffer = "a" + std::to_string(((NodeInt*)((TypeArray*)arg)->count->comptime())->value.to_int());
        Type* element = ((TypeArray*)arg)->element;

        while(element != nullptr && !instanceof<TypeBasic>(element) && !instanceof<TypeFunc>(element) && !instanceof<TypeStruct>(element)) {
            if(instanceof<TypeConst>(element)) {element = ((TypeConst*)element)->instance; continue;}

            if(instanceof<TypeArray>(element)) {
                buffer += "a";
                if(!instanceof<TypeVoid>(((TypeArray*)element)->element)) element = ((TypeArray*)element)->element;
            }
            else if(instanceof<TypePointer>(element)) {
                buffer += "p";
                if(!instanceof<TypeVoid>(((TypePointer*)element)->instance)) element = ((TypePointer*)element)->instance;
            }
        }

        if(element != nullptr) buffer += typeToString(element);
        return buffer;
    }
    else if(instanceof<TypeStruct>(arg)) {
        TypeStruct* ts = (TypeStruct*)arg;
        Type* t = ts;
        while(instanceof<TypeStruct>(t) && generator->toReplace.find(t->toString()) != generator->toReplace.end()) t = generator->toReplace[ts->toString()]->copy();
        if(!instanceof<TypeStruct>(t)) return typeToString(t);
        else ts = (TypeStruct*)t;

        for(int i=0; i<ts->types.size(); i++) {
            while(generator->toReplace.find(ts->types[i]->toString()) != generator->toReplace.end()) ts->types[i] = generator->toReplace[ts->types[i]->toString()]->copy();
        }
        if(ts->types.size() > 0) ts->updateByTypes();
        return "s-" + ts->toString();
    }
    else if(instanceof<TypeVector>(arg)) return "v" + typeToString(((TypeVector*)arg)->mainType) + std::to_string(((TypeVector*)arg)->count);
    else if(instanceof<TypeFunc>(arg)) return "func";
    else if(instanceof<TypeConst>(arg)) return typeToString(((TypeConst*)arg)->instance);
    return "";
}

std::string typesToString(std::vector<Type*> args) {
    std::string data = "[";
    for(int i=0; i<args.size(); i++) {
        data += "_" + typeToString(args[i]);
    }
    return data + "]";
}

std::string typesToString(std::vector<FuncArgSet> args) {
    std::vector<Type*> types;
    for(int i=0; i<args.size(); i++) types.push_back(args[i].type);
    return typesToString(types);
}

void AST::checkError(std::string message, int loc) {
    std::cout << "\033[0;31mError on " + std::to_string(loc) + " line: " + message + "\033[0;0m\n";
	exit(1);
}

LLVMGen::LLVMGen(std::string file, genSettings settings, nlohmann::json options) {
    this->file = file;
    this->settings = settings;
    this->options = options;
    this->context = LLVMContextCreate();
    this->lModule = LLVMModuleCreateWithNameInContext("rave", this->context);

    if(this->file.find('/') == std::string::npos && this->file.find('\\') == std::string::npos) this->file = "./" + this->file;

    if(settings.sse2 && settings.ssse3 && options["sse2"].template get<bool>() && options["ssse3"].template get<bool>()) {
        functions["llvm.x86.ssse3.phadd.d.128"] = {LLVMAddFunction(lModule, "llvm.x86.ssse3.phadd.d.128", LLVMFunctionType(
            LLVMVectorType(LLVMInt32TypeInContext(context), 4),
            std::vector<LLVMTypeRef>({LLVMVectorType(LLVMInt32TypeInContext(context), 4), LLVMVectorType(LLVMInt32TypeInContext(context), 4)}).data(),
            2, false
        )), new TypeFunc(
            new TypeVector(basicTypes[BasicType::Int], 4),
            {new TypeFuncArg(new TypeVector(basicTypes[BasicType::Int], 4), "v1"), new TypeFuncArg(new TypeVector(basicTypes[BasicType::Int], 4), "v2")},
            false
        )};

        functions["llvm.x86.ssse3.phadd.sw.128"] = {LLVMAddFunction(lModule, "llvm.x86.ssse3.phadd.sw.128", LLVMFunctionType(
            LLVMVectorType(LLVMInt16TypeInContext(context), 8),
            std::vector<LLVMTypeRef>({LLVMVectorType(LLVMInt16TypeInContext(context), 8), LLVMVectorType(LLVMInt16TypeInContext(context), 8)}).data(),
            2, false
        )), new TypeFunc(
            new TypeVector(basicTypes[BasicType::Short], 8),
            {new TypeFuncArg(new TypeVector(basicTypes[BasicType::Short], 8), "v1"), new TypeFuncArg(new TypeVector(basicTypes[BasicType::Short], 8), "v2")},
            false
        )};
    }

    if(settings.sse2 && options["sse2"].template get<bool>()) {
        functions["llvm.x86.sse3.hadd.ps"] = {LLVMAddFunction(lModule, "llvm.x86.sse3.hadd.ps", LLVMFunctionType(
            LLVMVectorType(LLVMFloatTypeInContext(context), 4),
            std::vector<LLVMTypeRef>({LLVMVectorType(LLVMFloatTypeInContext(context), 4), LLVMVectorType(LLVMFloatTypeInContext(context), 4)}).data(),
            2, false
        )), new TypeFunc(
            new TypeVector(basicTypes[BasicType::Float], 4),
            {new TypeFuncArg(new TypeVector(basicTypes[BasicType::Float], 4), "v1"), new TypeFuncArg(new TypeVector(basicTypes[BasicType::Float], 4), "v2")},
            false
        )};
    }

    this->neededFunctions["free"] = "std::free";
    this->neededFunctions["malloc"] = "std::malloc";
    this->neededFunctions["assert"] = "std::assert[_b_p]";
}

void LLVMGen::error(std::string msg, int line) {
    std::cout << "\033[0;31mError in '" + this->file + "' file at " + std::to_string(line) + " line: " + msg + "\033[0;0m" << std::endl;
    std::exit(1);
}

void LLVMGen::warning(std::string msg, int line) {
    std::cout << "\033[0;33mWarning in '" + this->file + "' file at " + std::to_string(line) + " line: " + msg + "\033[0;0m" << std::endl;
}

std::string LLVMGen::mangle(std::string name, bool isFunc, bool isMethod) {
    if(isFunc) {
        if(isMethod) return "_RaveM" + std::to_string(name.size()) + name; 
        return "_RaveF" + std::to_string(name.size()) + name;
    } return "_RaveG" + name;
}

Type* getTrueStructType(TypeStruct* ts) {
    Type* t = ts->copy();
    while(generator->toReplace.find(t->toString()) != generator->toReplace.end()) t = generator->toReplace[t->toString()];
    return t;
}

std::vector<Type*> getTrueTypeList(Type* t) {
    std::vector<Type*> buffer;
    while(instanceof<TypePointer>(t) || instanceof<TypeArray>(t)) {
        buffer.push_back(t);
        if(instanceof<TypePointer>(t)) t = ((TypePointer*)t)->instance;
        else t = ((TypeArray*)t)->element;
    } buffer.push_back(t);
    return buffer;
}

Type* LLVMGen::setByTypeList(std::vector<Type*> list) {
    std::vector<Type*> buffer = std::vector<Type*>(list);
    if(buffer.size() == 1) {
        if(instanceof<TypeStruct>(buffer[0])) return getTrueStructType((TypeStruct*)list[0]);
        return buffer[0];
    }
    if(instanceof<TypeStruct>(buffer[buffer.size()-1])) buffer[buffer.size()-1] = getTrueStructType((TypeStruct*)buffer[buffer.size()-1]);
    for(int i=buffer.size()-1; i>0; i--) {
        if(instanceof<TypePointer>(buffer[i-1])) ((TypePointer*)buffer[i-1])->instance = buffer[i];
        else if(instanceof<TypeArray>(buffer[i-1])) ((TypeArray*)buffer[i-1])->element = buffer[i];
    }
    return buffer[0];
}

LLVMTypeRef LLVMGen::genType(Type* type, int loc) {
    if(type == nullptr) return LLVMPointerType(LLVMInt8TypeInContext(this->context), 0);
    if(instanceof<TypeByval>(type)) return LLVMPointerType(generator->genType(type->getElType(), loc), 0);
    if(instanceof<TypeAlias>(type)) return nullptr;
    if(instanceof<TypeBasic>(type)) switch(((TypeBasic*)type)->type) {
        case BasicType::Bool: return LLVMInt1TypeInContext(this->context);
        case BasicType::Char: case BasicType::Uchar: return LLVMInt8TypeInContext(this->context);
        case BasicType::Short: case BasicType::Ushort: return LLVMInt16TypeInContext(this->context);
        case BasicType::Half: return LLVMHalfTypeInContext(this->context);
        case BasicType::Bhalf: return LLVMBFloatTypeInContext(this->context);
        case BasicType::Int: case BasicType::Uint: return LLVMInt32TypeInContext(this->context);
        case BasicType::Long: case BasicType::Ulong: return LLVMInt64TypeInContext(this->context);
        case BasicType::Cent: case BasicType::Ucent: return LLVMInt128TypeInContext(this->context);
        case BasicType::Float: return LLVMFloatTypeInContext(this->context);
        case BasicType::Double: return LLVMDoubleTypeInContext(this->context);
        case BasicType::Real: return LLVMFP128TypeInContext(this->context);
        default: return nullptr;
    }
    if(instanceof<TypePointer>(type)) {
        if(instanceof<TypeVoid>(((TypePointer*)type)->instance)) return LLVMPointerType(LLVMInt8TypeInContext(this->context),0);
        return LLVMPointerType(this->genType(((TypePointer*)type)->instance, loc),0);
    }
    if(instanceof<TypeArray>(type)) return LLVMArrayType(this->genType(((TypeArray*)type)->element, loc), ((NodeInt*)((TypeArray*)type)->count->comptime())->value.to_int());
    if(instanceof<TypeStruct>(type)) {
        TypeStruct* s = (TypeStruct*)type;
        if(this->structures.find(s->name) == this->structures.end()) {
            if(AST::structTable.find(s->name) != AST::structTable.end() && AST::structTable[s->name]->templateNames.size() > 0) {
                generator->error("trying to generate template structure without templates!", loc);
            }

            if(this->toReplace.find(s->name) != this->toReplace.end()) {
                return this->genType(this->toReplace[s->name], loc);
            }

            if(s->name.find('<') != std::string::npos) {
                TypeStruct* sCopy = (TypeStruct*)s->copy();
                for(int i=0; i<sCopy->types.size(); i++) sCopy->types[i] = this->setByTypeList(getTrueTypeList(sCopy->types[i]));
                sCopy->updateByTypes();
                if(this->structures.find(sCopy->name) != this->structures.end()) return this->structures[sCopy->name];
                std::string origStruct = sCopy->name.substr(0, sCopy->name.find('<'));
                
                if(AST::structTable.find(origStruct) != AST::structTable.end()) {
                    return AST::structTable[origStruct]->genWithTemplate(sCopy->name.substr(sCopy->name.find('<'), sCopy->name.size()), sCopy->types);
                }
            }

            if(AST::structTable.find(s->name) != AST::structTable.end()) {
                AST::structTable[s->name]->check();
                AST::structTable[s->name]->generate();
                return this->genType(type, loc);
            }
            else if(AST::aliasTypes.find(s->name) != AST::aliasTypes.end()) return this->genType(AST::aliasTypes[s->name], loc);
            
            this->error("unknown structure '" + s->name + "'!", loc);
        }
        return this->structures[s->name];
    }
    if(instanceof<TypeVoid>(type)) return LLVMVoidTypeInContext(this->context);
    if(instanceof<TypeFunc>(type)) {
        TypeFunc* tf = (TypeFunc*)type;
        if(instanceof<TypeVoid>(tf->main)) tf->main = basicTypes[BasicType::Char];
        std::vector<LLVMTypeRef> types;
        for(int i=0; i<tf->args.size(); i++) types.push_back(this->genType(tf->args[i]->type, loc));
        return LLVMPointerType(LLVMFunctionType(this->genType(tf->main, loc), types.data(), types.size(), false), 0);
    }
    if(instanceof<TypeFuncArg>(type)) return this->genType(((TypeFuncArg*)type)->type, loc);
    if(instanceof<TypeConst>(type)) return this->genType(((TypeConst*)type)->instance, loc);
    if(instanceof<TypeVector>(type)) return LLVMVectorType(this->genType(((TypeVector*)type)->mainType, loc), ((TypeVector*)type)->count);
    if(instanceof<TypeLLVM>(type)) return ((TypeLLVM*)type)->tr;
    this->error("undefined type!", loc);
    return nullptr;
}

RaveValue LLVMGen::byIndex(RaveValue value, std::vector<LLVMValueRef> indexes) {
    if(instanceof<TypeArray>(value.type)) return byIndex(
        LLVM::gep(value, std::vector<LLVMValueRef>({LLVMConstInt(LLVMInt32TypeInContext(generator->context), 0, false)}).data(), 2, "gep_byIndex"), indexes
    );

    if(instanceof<TypeArray>(value.type->getElType())) {
        Type* newType = new TypePointer(value.type->getElType()->getElType());
        value.type = newType;
    }

    if(indexes.size() > 1) {
        RaveValue oneGep = LLVM::gep(value, std::vector<LLVMValueRef>({indexes[0]}).data(), 1, "gep2_byIndex");
        return byIndex(oneGep, std::vector<LLVMValueRef>(indexes.begin() + 1, indexes.end()));
    }

    return LLVM::gep(value, indexes.data(), indexes.size(), "gep3_byIndex");
}

void LLVMGen::addAttr(std::string name, LLVMAttributeIndex index, LLVMValueRef ptr, int loc, unsigned long value) {
    int id = LLVMGetEnumAttributeKindForName(name.c_str(), name.size());
    if(id == 0) this->error("unknown attribute '" + name + "'!", loc);
    LLVMAddAttributeAtIndex(ptr, index, LLVMCreateEnumAttribute(generator->context, id, value));
}

void LLVMGen::addTypeAttr(std::string name, LLVMAttributeIndex index, LLVMValueRef ptr, int loc, LLVMTypeRef value) {
    LLVMAttributeRef attr = LLVMCreateTypeAttribute(generator->context, LLVMGetEnumAttributeKindForName(name.c_str(), name.size()), value);
    if(attr == nullptr) this->error("unknown attribute '" + name + "!", loc);
    LLVMAddAttributeAtIndex(ptr, index, attr);
}

void LLVMGen::addStrAttr(std::string name, LLVMAttributeIndex index, LLVMValueRef ptr, int loc, std::string value) {
    LLVMAttributeRef attr = LLVMCreateStringAttribute(generator->context, name.c_str(), name.size(), value.c_str(), value.size());
    if(attr == nullptr) this->error("unknown attribute '" + name + "'!", loc);
    LLVMAddAttributeAtIndex(ptr, index, attr);
}

int LLVMGen::getAlignment(Type* type) {
    if(instanceof<TypeBasic>(type)) {
        switch(((TypeBasic*)type)->type) {
            case BasicType::Bool: case BasicType::Char: case BasicType::Uchar: return 1;
            case BasicType::Short: case BasicType::Ushort: case BasicType::Half: case BasicType::Bhalf: return 2;
            case BasicType::Int: case BasicType::Uint: case BasicType::Float: return 4;
            case BasicType::Long: case BasicType::Ulong: case BasicType::Double: return 8;
            case BasicType::Cent: case BasicType::Ucent: case BasicType::Real: return 16;
            default: return 0;
        }
    }
    else if(instanceof<TypePointer>(type)) return 8;
    else if(instanceof<TypeArray>(type)) return this->getAlignment(((TypeArray*)type)->element);
    else if(instanceof<TypeStruct>(type)) return 8;
    else if(instanceof<TypeFunc>(type)) return this->getAlignment(((TypeFunc*)type)->main);
    else if(instanceof<TypeConst>(type)) return this->getAlignment(((TypeConst*)type)->instance);
    return 0;
}

// Scope

Scope::Scope(std::string funcName, std::map<std::string, int> args, std::map<std::string, NodeVar*> argVars) {
    this->funcName = funcName;
    this->args = std::map<std::string, int>(args);
    this->argVars = std::map<std::string, NodeVar*>(argVars);
    this->aliasTable = std::map<std::string, Node*>();
    this->localScope = std::map<std::string, RaveValue>();
    this->localVars = std::map<std::string, NodeVar*>();
}

void Scope::remove(std::string name) {
    if(this->localScope.find(name) != this->localScope.end()) {
        this->localScope.erase(name);
        this->localVars.erase(name);
    }
    else if(this->aliasTable.find(name) != this->aliasTable.end()) this->aliasTable.erase(name);
}

RaveValue Scope::get(std::string name, int loc) {
    RaveValue value = {nullptr, nullptr};

    if(AST::aliasTable.find(name) != AST::aliasTable.end()) value = AST::aliasTable[name]->generate();
    else if(generator->toReplaceValues.find(name) != generator->toReplaceValues.end()) value = generator->toReplaceValues[name]->generate();
    else if(this->aliasTable.find(name) != this->aliasTable.end()) value = this->aliasTable[name]->generate();
    else if(localScope.find(name) != localScope.end()) value = localScope[name];
    else if(generator->globals.find(name) != generator->globals.end()) value = generator->globals[name];
    else if(generator->functions.find(this->funcName) != generator->functions.end()) {
        if(this->args.find(name) == this->args.end()) {
            if(generator->functions.find(name) != generator->functions.end()) generator->error(name, loc);
        }
        else return {LLVMGetParam(generator->functions[this->funcName].value, this->args[name]), AST::funcTable[this->funcName]->getArgType(name)};
    }
    
    if(value.value == nullptr && hasAtThis(name)) {
        NodeVar* nv = this->getVar("this", loc);
        TypeStruct* ts = nullptr;
        if(instanceof<TypeStruct>(nv->type)) ts = (TypeStruct*)nv->type;
        else if(instanceof<TypePointer>(nv->type) && instanceof<TypeStruct>(((TypePointer*)nv->type)->instance)) ts = (TypeStruct*)(((TypePointer*)nv->type)->instance);
        if(ts != nullptr && AST::structTable.find(ts->toString()) != AST::structTable.end() &&
           AST::structsNumbers.find({ts->toString(), name}) != AST::structsNumbers.end()) {
            NodeGet* nget = new NodeGet(new NodeIden("this", loc), name, true, loc);
            value = nget->generate();
        }
    }

    if(value.value == nullptr) generator->error("undefined identifier '" + name + "' at function '" + this->funcName + "'!", loc);
    if(instanceof<TypePointer>(value.type)) value = LLVM::load(value, "scopeGetLoad", loc);
    return value;
}

RaveValue Scope::getWithoutLoad(std::string name, int loc) {
    if(generator->toReplaceValues.find(name) != generator->toReplaceValues.end()) return generator->toReplaceValues[name]->generate();
    if(AST::aliasTable.find(name) != AST::aliasTable.end()) return AST::aliasTable[name]->generate();
    if(this->aliasTable.find(name) != this->aliasTable.end()) return this->aliasTable[name]->generate();
    if(this->localScope.find(name) != this->localScope.end()) return this->localScope[name];
    if(generator->globals.find(name) != generator->globals.end()) return generator->globals[name];
    if(hasAtThis(name)) {
        NodeVar* nv = this->getVar("this", loc);
        TypeStruct* ts = nullptr;
        if(instanceof<TypeStruct>(nv->type)) ts = (TypeStruct*)nv->type;
        else if(instanceof<TypePointer>(nv->type) && instanceof<TypeStruct>(((TypePointer*)nv->type)->instance)) ts = (TypeStruct*)(((TypePointer*)nv->type)->instance);
        if(ts != nullptr && AST::structTable.find(ts->toString()) != AST::structTable.end() &&
           AST::structsNumbers.find({ts->toString(), name}) != AST::structsNumbers.end()) {
            NodeGet* nget = new NodeGet(new NodeIden("this", loc), name, true, loc);
            return nget->generate();
        }
    }
    if(this->args.find(name) == this->args.end()) generator->error("undefined identifier '" + name + "' at function '" + this->funcName + "'!", loc);
    return {LLVMGetParam(generator->functions[this->funcName].value, this->args[name]), AST::funcTable[this->funcName]->getArgType(name)};
}

bool Scope::has(std::string name) {
    return AST::aliasTable.find(name) != AST::aliasTable.end() ||
        this->aliasTable.find(name) != this->aliasTable.end() ||
        this->localScope.find(name) != this->localScope.end() ||
        generator->globals.find(name) != generator->globals.end() ||
        this->args.find(name) != this->args.end();
}

bool Scope::hasAtThis(std::string name) {
    if(this->has("this") && (AST::funcTable.find(this->funcName) != AST::funcTable.end() && AST::funcTable[this->funcName]->isMethod)) {
        NodeVar* nv = this->getVar("this", -1);
        TypeStruct* ts = nullptr;
        if(instanceof<TypeStruct>(nv->type)) ts = (TypeStruct*)nv->type;
        else if(instanceof<TypePointer>(nv->type) && instanceof<TypeStruct>(((TypePointer*)nv->type)->instance)) ts = (TypeStruct*)(((TypePointer*)nv->type)->instance);
        return(ts != nullptr && AST::structTable.find(ts->toString()) != AST::structTable.end() && AST::structsNumbers.find({ts->toString(), name}) != AST::structsNumbers.end());
    }
    return false;
}

bool Scope::locatedAtThis(std::string name) {
    if(AST::aliasTable.find(name) != AST::aliasTable.end()) return false;
    if(this->aliasTable.find(name) != this->aliasTable.end()) return false;
    if(this->localScope.find(name) != this->localScope.end()) return false;
    if(generator->globals.find(name) != generator->globals.end()) return false;
    return this->hasAtThis(name);
}

NodeVar* Scope::getVar(std::string name, int loc) {
    if(this->localVars.find(name) != this->localVars.end()) return this->localVars[name];
    if(AST::varTable.find(name) != AST::varTable.end()) return AST::varTable[name];
    if(this->argVars.find(name) != this->argVars.end()) {
        this->argVars[name]->isUsed = true;
        return this->argVars[name];
    }
    if(this->aliasTable.find(name) != this->aliasTable.end()) return (new NodeVar(name, this->aliasTable[name]->copy(), false, false, false, {}, loc, this->aliasTable[name]->getType()));
    if(this->has("this") && (AST::funcTable.find(this->funcName) != AST::funcTable.end() && AST::funcTable[this->funcName]->isMethod)) {
        NodeVar* nv = this->getVar("this", loc);
        TypeStruct* ts = nullptr;
        if(instanceof<TypeStruct>(nv->type)) ts = (TypeStruct*)nv->type;
        else if(instanceof<TypePointer>(nv->type) && instanceof<TypeStruct>(((TypePointer*)nv->type)->instance)) ts = (TypeStruct*)(((TypePointer*)nv->type)->instance);
        if(ts != nullptr && AST::structTable.find(ts->toString()) != AST::structTable.end() &&
           AST::structsNumbers.find({ts->toString(), name}) != AST::structsNumbers.end()) {
            return AST::structsNumbers[{ts->toString(), name}].var;
        }
    }
    generator->error("undefined variable '" + name + "'!", loc);
    return nullptr;
}

void Scope::hasChanged(std::string name) {
    if(this->localVars.find(name) != this->localVars.end()) ((NodeVar*)this->localVars[name])->isChanged = true;
    if(AST::varTable.find(name) != AST::varTable.end()) AST::varTable[name]->isChanged = true;
    if(this->argVars.find(name) != this->argVars.end()) ((NodeVar*)this->argVars[name])->isChanged = true;
}

Scope* copyScope(Scope* original) {
    Scope* newScope = new Scope(original->funcName, original->args, original->argVars);
    newScope->fnEnd = original->fnEnd;
    newScope->funcHasRet = original->funcHasRet;
    newScope->localVars = original->localVars;
    newScope->localScope = original->localScope;
    newScope->aliasTable = original->aliasTable;
    return newScope;
}

std::string typeToString(LLVMTypeRef type) {return std::string(LLVMPrintTypeToString(type));}
