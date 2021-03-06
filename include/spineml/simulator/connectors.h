#pragma once

// Standard C++ includes
#include <vector>

// Forward declarations
struct SparseProjection;

namespace pugi
{
    class xml_node;
}

namespace filesystem
{
    class path;
}

//------------------------------------------------------------------------
// SpineMLSimulator::Connectors
//------------------------------------------------------------------------
namespace SpineMLSimulator
{
namespace Connectors
{
    unsigned int create(const pugi::xml_node &node, unsigned int numPre, unsigned int numPost,
                        unsigned int **rowLength, unsigned int **ind, const unsigned int *maxRowLength,
                        const filesystem::path &basePath, std::vector<unsigned int> &remapIndices);
}   // namespace Connectors
}   // namespace SpineMLSimulator
