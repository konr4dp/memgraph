#pragma once

#include "code.hpp"
#include "cypher/visitor/traverser.hpp"

class ReadTraverser : public Traverser, public Code
{
};
