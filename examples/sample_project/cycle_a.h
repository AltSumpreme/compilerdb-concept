#pragma once

// Deliberate circular include pair (with cycle_b.h). CompileGraphDB's
// Review II version stores these edges like any other; detecting and
// reporting the cycle is remaining work for the final review.

#include "cycle_b.h"

namespace cycle { inline int a_value() { return 1; } }
