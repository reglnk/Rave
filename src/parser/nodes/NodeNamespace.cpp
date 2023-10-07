/*
This Source Code Form is subject to the terms of the Mozilla
Public License, v. 2.0. If a copy of the MPL was not distributed
with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include "../../include/parser/nodes/NodeNamespace.hpp"
#include "../../include/parser/nodes/NodeVar.hpp"
#include "../../include/parser/nodes/NodeFunc.hpp"
#include "../../include/parser/nodes/NodeStruct.hpp"
#include "../../include/parser/nodes/NodeAliasType.hpp"
#include "../../include/utils.hpp"
#include <string>

NodeNamespace::NodeNamespace(std::string name, std::vector<Node*> nodes, long loc) {
    this->loc = loc;
    this->nodes = std::vector<Node*>(nodes);
    this->names = std::vector<std::string>();
    this->names.push_back(name);
}

NodeNamespace::NodeNamespace(std::vector<std::string> names, std::vector<Node*> nodes, long loc) {
    this->loc = loc;
    this->nodes = std::vector<Node*>(nodes);
    this->names = std::vector<std::string>();
    for(int i=0; i<names.size(); i++) this->names.push_back(names[i]);
}

Node* NodeNamespace::copy() {
    std::vector<Node*> cNodes;
    for(int i=0; i<this->nodes.size(); i++) cNodes.push_back(this->nodes[i]);
    return new NodeNamespace(std::vector<std::string>(this->names), cNodes, this->loc);
}

Type* NodeNamespace::getType() {return new TypeVoid();}
Node* NodeNamespace::comptime() {return nullptr;}

void NodeNamespace::check() {
    bool oldCheck = this->isChecked;
    this->isChecked = true;

    if(!oldCheck) for(int i=0; i<this->nodes.size(); i++) {
        if(this->nodes[i] == nullptr) continue;
        if(instanceof<NodeFunc>(this->nodes[i])) {
            NodeFunc* nfunc = (NodeFunc*)this->nodes[i];
            if(hidePrivated && nfunc->isPrivate) continue;
            for(int i=0; i<this->names.size(); i++) nfunc->namespacesNames.insert(nfunc->namespacesNames.begin(), this->names[i]);
            this->nodes[i]->check();
        }
        else if(instanceof<NodeNamespace>(this->nodes[i])) {
            NodeNamespace* nnamespace = (NodeNamespace*)this->nodes[i];
            for(int i=0; i<this->names.size(); i++) nnamespace->names.insert(nnamespace->names.begin(), this->names[i]);
            this->nodes[i]->check();
        }
        else if(instanceof<NodeVar>(this->nodes[i])) {
            NodeVar* nvar = (NodeVar*)this->nodes[i];
            if(hidePrivated && nvar->isPrivate) continue;
            for(int i=0; i<this->names.size(); i++) nvar->namespacesNames.insert(nvar->namespacesNames.begin(), this->names[i]);
            this->nodes[i]->check();
        }
        else if(instanceof<NodeStruct>(this->nodes[i])) {
            NodeStruct* structure = ((NodeStruct*)this->nodes[i]);
            for(int i=0; i<this->names.size(); i++) structure->namespacesNames.insert(structure->namespacesNames.begin(), this->names[i]);
            this->nodes[i]->check();
        }
        else if(instanceof<NodeAliasType>(this->nodes[i])) {
            NodeAliasType* naliastype = (NodeAliasType*)this->nodes[i];
            for(int i=0; i<this->names.size(); i++) naliastype->namespacesNames.insert(naliastype->namespacesNames.begin(), this->names[i]);
            this->nodes[i]->check();
        }
    }
}

LLVMValueRef NodeNamespace::generate() {
    for(int i=0; i<this->nodes.size(); i++) {
        if(this->nodes[i] == nullptr) continue;
        if(instanceof<NodeFunc>(this->nodes[i])) {
            if(hidePrivated && ((NodeFunc*)nodes[i])->isPrivate) continue;
            if(!this->nodes[i]->isChecked) {
                for(int i=0; i<this->names.size(); i++) ((NodeFunc*)nodes[i])->namespacesNames.push_back(this->names[i]);
                this->nodes[i]->check();
            }
            if(!((NodeFunc*)this->nodes[i])->isExtern) ((NodeFunc*)this->nodes[i])->isExtern = this->isImported;
            this->nodes[i]->generate();
        }
        else if(instanceof<NodeNamespace>(this->nodes[i])) {
            if(!this->nodes[i]->isChecked) {
                for(int i=0; i<this->names.size(); i++) ((NodeNamespace*)nodes[i])->names.push_back(this->names[i]);
                this->nodes[i]->check();
            }
            if(!((NodeNamespace*)this->nodes[i])->isImported) ((NodeNamespace*)this->nodes[i])->isImported = this->isImported;
            this->nodes[i]->generate();
        }
        else if(instanceof<NodeVar>(this->nodes[i])) {
            if(hidePrivated && ((NodeVar*)nodes[i])->isPrivate) continue;
            if(!this->nodes[i]->isChecked) {
                for(int i=0; i<this->names.size(); i++) ((NodeVar*)nodes[i])->namespacesNames.push_back(this->names[i]);
                this->nodes[i]->check();
            }
            if(!((NodeVar*)this->nodes[i])->isExtern) ((NodeVar*)this->nodes[i])->isExtern = this->isImported;
            this->nodes[i]->generate();
        }
        else if(instanceof<NodeStruct>(this->nodes[i])) {
            if(!this->nodes[i]->isChecked) {
                for(int i=0; i<this->names.size(); i++) ((NodeStruct*)nodes[i])->namespacesNames.push_back(this->names[i]);
                this->nodes[i]->check();
            }
            if(!((NodeStruct*)this->nodes[i])->isImported) ((NodeStruct*)this->nodes[i])->isImported = this->isImported;
            this->nodes[i]->generate();
        }
        else if(instanceof<NodeAliasType>(this->nodes[i])) {
            if(!this->nodes[i]->isChecked) {
                for(int i=0; i<this->names.size(); i++) ((NodeAliasType*)nodes[i])->namespacesNames.push_back(this->names[i]);
                this->nodes[i]->check();
            }
            this->nodes[i]->generate();
        }
    }
    return nullptr;
}