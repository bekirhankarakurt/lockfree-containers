// SPDX-License-Identifier: MIT
/// @file version.hpp
/// @brief Library version macros.
#pragma once

/// Version string, usable in preprocessor-free contexts such as printf.
#define LFC_VERSION_STRING "1.0.0"

namespace lfc {

inline constexpr int kVersionMajor = 1;
inline constexpr int kVersionMinor = 0;
inline constexpr int kVersionPatch = 0;

}  // namespace lfc
