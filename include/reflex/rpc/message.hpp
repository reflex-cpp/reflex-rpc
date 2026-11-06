// Part of the Reflex project.
// Copyright © Reflex Project Contributors.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <boost/json/value.hpp>

namespace reflex::rpc {

struct [[reflex::reflect]] call_message_s {
    std::string fn;
    std::vector<boost::json::value> args;
};

struct [[reflex::reflect]] response_message_s {
    std::optional<std::string> error = std::nullopt;
    boost::json::value result{};
};

} // namespace reflex::rpc

#ifndef __REFLEX_TOOL__
    #include <reflex/rpc/message.meta.inc>
#endif
