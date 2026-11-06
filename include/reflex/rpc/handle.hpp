// Part of the Reflex project.
// Copyright © Reflex Project Contributors.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <reflex/rpc/reflex_properties.hpp>

namespace reflex::rpc {

class RpcServer;

struct [[reflex::reflect]] object_handle_s {
    std::uint64_t id;
    std::string type;
    std::uint32_t generation;

    bool operator==(const object_handle_s &other) const = default;
};

template<typename HandleType>
struct [[reflex::reflect]] typed_object_handle_s : object_handle_s {
    using handle_type = HandleType;

    explicit typed_object_handle_s(object_handle_s &&untyped) noexcept : object_handle_s(std::move(untyped)) {}

    typed_object_handle_s(const typed_object_handle_s &other) = default;
    typed_object_handle_s(typed_object_handle_s &&other) = default;

    typed_object_handle_s &operator=(const typed_object_handle_s &other) = default;
    typed_object_handle_s &operator=(typed_object_handle_s &&other) = default;
    bool operator==(const typed_object_handle_s &other) const = default;

    explicit(false) typed_object_handle_s(const typed_object_handle_s<std::remove_const_t<HandleType>> &non_const)
        requires std::is_const_v<HandleType>
        : object_handle_s(non_const) {}

    explicit(false) typed_object_handle_s(typed_object_handle_s<std::remove_const_t<HandleType>> &&non_const) noexcept
        requires std::is_const_v<HandleType>
        : object_handle_s(std::move(non_const)) {}

    typed_object_handle_s &operator=(const typed_object_handle_s<std::remove_const_t<HandleType>> &non_const)
        requires std::is_const_v<HandleType>
    {
        static_cast<object_handle_s &>(*this) = non_const;
        return *this;
    }

    typed_object_handle_s &operator=(typed_object_handle_s<std::remove_const_t<HandleType>> &&non_const) noexcept
        requires std::is_const_v<HandleType>
    {
        static_cast<object_handle_s &>(*this) = std::move(non_const);
        return *this;
    }
};

struct rpc_server_ctx_s {
    RpcServer &rpc_server;
};

[[reflex::reflect]] void reset_object_handle([[reflex::prop(rpc::implicit)]] rpc_server_ctx_s context,
                                             object_handle_s handle);

[[reflex::reflect]] std::vector<std::string> get_registered_functions(
        [[reflex::prop(rpc::implicit)]] rpc_server_ctx_s context);

} // namespace reflex::rpc

#ifndef __REFLEX_TOOL__
    #include <reflex/rpc/handle.meta.inc>
#endif
