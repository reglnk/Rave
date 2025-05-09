/*
This Source Code Form is subject to the terms of the Mozilla
Public License, v. 2.0. If a copy of the MPL was not distributed
with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#pragma once

#include <llvm-c/Core.h>
#include "Node.hpp"
#include "../Types.hpp"

class NodeItop : public Node {
public:
    Node* value;
    Type* type;
    int loc;

    NodeItop(Node* value, Type* type, int loc);
    Type* getType() override;
    RaveValue generate() override;
    Node* copy() override;
    Node* comptime() override;
    void check() override;
    ~NodeItop() override;
};