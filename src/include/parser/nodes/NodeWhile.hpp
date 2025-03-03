/*
This Source Code Form is subject to the terms of the Mozilla
Public License, v. 2.0. If a copy of the MPL was not distributed
with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#pragma once

#include "../Types.hpp"
#include <llvm-c/Core.h>
#include "Node.hpp"
#include <vector>
#include <string>

class NodeWhile : public Node {
public:
    Node* cond;
    Node* body;
    int loc;

    NodeWhile(Node* cond, Node* body, int loc);
    void optimize() override;
    Type* getType() override;
    RaveValue generate() override;
    Node* copy() override;
    Node* comptime() override;
    void check() override;
    ~NodeWhile() override;
};