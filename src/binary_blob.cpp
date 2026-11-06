// Part of the Reflex project.
// Copyright © Reflex Project Contributors.
// SPDX-License-Identifier: Apache-2.0

#include <reflex/rpc/binary_blob.hpp>

#include <reflex/config/deserialize.hpp>
#include <reflex/config/serialize.hpp>
#include <reflex/rpc/common.hpp>

namespace reflex::config {

expected_t<boost::json::value> custom_traits<rpc::binary_blob_s>::try_serialize(
        Context &context,
        const context_entry_s &entry,
        const rpc::binary_blob_s &value,
        const boost::json::storage_ptr &memory_resource) {
    const auto &ctx = static_cast<rpc::RpcSerializationContext &>(context);
    auto &blob_storage = ctx.blob_storage;

    const auto blob_id = blob_storage.add_blob(value.bytes);
    return try_serialize_value(context, entry, blob_id, memory_resource);
}

expected_t<rpc::binary_blob_s> custom_traits<rpc::binary_blob_s>::try_deserialize(DeserializationContext &context,
                                                                                  const context_entry_s &entry,
                                                                                  const boost::json::value &value) {
    if (auto exp_blob_id = try_deserialize_value<rpc::binary_blob_id_s>(context, entry, value)) {
        auto &ctx = static_cast<rpc::RpcDeserializationContext &>(context);
        auto &blob_storage = ctx.blob_storage;

        const auto blob = blob_storage.get_blob(*std::move(exp_blob_id));
        return rpc::binary_blob_s{.bytes = std::vector(blob.begin(), blob.end())};
    } else {
        return std::unexpected{std::move(exp_blob_id).error()};
    }
}

} // namespace reflex::config