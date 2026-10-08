#ifndef _MODEL_SAMPLE_HPP_
#define _MODEL_SAMPLE_HPP_

#include "Model/Model.hpp"

/// @file Sample.hpp
/// @brief Bundled example scenario.

namespace ad
{

/// A -> B (unavailable) -> 5 s timeout with retries -> fallback to C -> 200 OK.
Model SampleModel();

/// Empty diagram.
Model EmptyModel();

} // namespace ad

#endif // _MODEL_SAMPLE_HPP_
