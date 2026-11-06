// Part of the Reflex project.
// Copyright © Reflex Project Contributors.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <reflex/meta.hpp>

namespace reflex::rpc {

struct implicit_t {
    explicit constexpr implicit_t() = default;
};

inline constexpr auto implicit = implicit_t{};

template<typename ParamMeta>
[[nodiscard]] constexpr bool is_implicit() noexcept {
    return reflex::has_property<implicit_t, decltype(ParamMeta::properties)>();
}

} // namespace reflex::rpc