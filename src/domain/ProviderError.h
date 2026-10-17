#pragma once

#include <cstdint>

// Typed reason why an Answer could not be completed. Kept separate from prose
// details so callers can branch on the kind of failure without parsing text.
enum class ProviderError : std::uint8_t {
    kNetwork,
    kAuthentication,
    kRateLimit,
    kCancelled,
    kInvalidResponse,
    kUnknown,
};
