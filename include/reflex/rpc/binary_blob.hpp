// Part of the Reflex project.
// Copyright © Reflex Project Contributors.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include <boost/json/fwd.hpp>

#include <reflex/config/context.hpp>
#include <reflex/config/errors.hpp>
#include <reflex/config/traits.hpp>

namespace reflex::rpc {

struct binary_blob_s {
    std::vector<std::byte> bytes;
};

struct [[reflex::reflect]] binary_blob_id_s {
    std::size_t blob_id;
};


} // namespace reflex::rpc

namespace reflex::config {

template<>
struct custom_traits<rpc::binary_blob_s> {
    static expected_t<boost::json::value> try_serialize(Context &context,
                                                        const context_entry_s &entry,
                                                        const rpc::binary_blob_s &value,
                                                        const boost::json::storage_ptr &memory_resource);

    static expected_t<rpc::binary_blob_s> try_deserialize(DeserializationContext &context,
                                                          const context_entry_s &entry,
                                                          const boost::json::value &value);

    static expected_t<void> try_validate(Context & /*context*/,
                                         const context_entry_s & /*entry*/,
                                         const rpc::binary_blob_s & /*value*/) {
        return {};
    }
};

} // namespace reflex::config

#ifndef __REFLEX_TOOL__
    #include <reflex/rpc/binary_blob.meta.inc>
#endif
