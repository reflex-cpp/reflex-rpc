// Part of the Reflex project.
// Copyright © Reflex Project Contributors.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <boost/json/value.hpp>
#include <unifex/sender_concepts.hpp>

#include <reflex/config/context.hpp>
#include <reflex/config/deserialize.hpp>
#include <reflex/config/detail/throw_formatted.hpp>
#include <reflex/config/serialize.hpp>

#include <reflex/rpc/binary_blob.hpp>
#include <reflex/rpc/handle.hpp>

namespace reflex::rpc {

class Error : public std::runtime_error {
public:
    explicit Error(const char *message);
};

struct object_ptr_s {
    void *raw_ptr = nullptr;
    std::shared_ptr<void> smart_ptr{};
    std::uint32_t generation = 0;
    std::uint64_t global_idx = 0;
};

class HandleStorage {
public:
    void reset_handle(const object_handle_s &handle);

    [[nodiscard]] object_handle_s add_handle(void *raw_ptr,
                                             std::shared_ptr<void> smart_ptr,
                                             std::string_view handle_type_name);

    object_ptr_s get_pointer(const object_handle_s &handle, std::string_view type_name) const;

    ~HandleStorage();

private:
    void reserve_handles();

    mutable std::mutex m_mutex{};
    std::vector<object_ptr_s> m_handles;
    std::vector<std::size_t> m_free_indices;
    std::uint64_t m_global_idx = 0;
};

class InputBinaryBlobStorage {
public:
    using blob_t = std::span<const std::byte>;
    using blobs_t = std::span<const blob_t>;

    explicit InputBinaryBlobStorage() {}
    explicit InputBinaryBlobStorage(blobs_t blobs) : m_blobs{blobs} {}

    const blob_t &get_blob(binary_blob_id_s blob_id) const {
        if (blob_id.blob_id >= m_blobs.size()) {
            config::detail::throw_formatted<Error>("Can`t find blob with id = {}", blob_id.blob_id);
        }
        return m_blobs[blob_id.blob_id];
    }

private:
    blobs_t m_blobs;
};

class OutputBinaryBlobStorage {
public:
    using blob_t = std::vector<std::byte>;
    using blobs_t = std::vector<blob_t>;

    binary_blob_id_s add_blob(blob_t blob) {
        m_blobs.push_back(std::move(blob));
        return binary_blob_id_s{m_blobs.size() - 1};
    }
    blobs_t &get_blobs() { return m_blobs; }

private:
    blobs_t m_blobs;
};

class RpcDeserializationContext : public config::DeserializationContext {
public:
    RpcDeserializationContext(HandleStorage &handle_storage_rhs, const InputBinaryBlobStorage &blob_storage_rhs)
        : handle_storage{handle_storage_rhs},
          blob_storage{blob_storage_rhs} {}

    HandleStorage &handle_storage;
    const InputBinaryBlobStorage &blob_storage;
};

class RpcSerializationContext : public config::Context {
public:
    RpcSerializationContext(HandleStorage &handle_storage_rhs, OutputBinaryBlobStorage &blob_storage_rhs)
        : handle_storage{handle_storage_rhs},
          blob_storage{blob_storage_rhs} {}

    HandleStorage &handle_storage;
    OutputBinaryBlobStorage &blob_storage;
};

namespace detail {

template<typename T>
struct return_type {
    using type = T;
};

template<unifex::sender T>
struct return_type<T> {
    using type = unifex::sender_single_value_return_type_t<T>;
};

template<typename T>
using return_type_t = typename return_type<T>::type;

template<typename Meta>
concept is_member_function = requires { Meta::ptr; };

template<typename FunctionMeta>
consteval auto function_argument_count() {
    using parameters = decltype(FunctionMeta::parameters);
    static constexpr auto arg_count = std::tuple_size_v<parameters>;
    static constexpr auto this_offset = std::conditional_t<is_member_function<FunctionMeta>,
                                                           std::integral_constant<std::size_t, 1>,
                                                           std::integral_constant<std::size_t, 0>>::value;

    if constexpr (arg_count > 0) {
        if constexpr (rpc::is_implicit<std::tuple_element_t<0, parameters>>()) {
            return arg_count + this_offset - 1;
        }
    }

    return arg_count + this_offset;
}

template<typename FunctionMeta>
consteval auto function_argument_offset() {
    using parameters = decltype(FunctionMeta::parameters);
    constexpr auto arg_count = std::tuple_size_v<parameters>;

    if constexpr (arg_count > 0) {
        if constexpr (rpc::is_implicit<std::tuple_element_t<0, parameters>>()) {
            return 1;
        }
    }
    return 0;
}

} // namespace detail

template<typename T>
struct rpc_type {
    using type = T;
};

template<typename T>
struct rpc_type<std::shared_ptr<T>> {
    using type = typed_object_handle_s<T>;
};

template<typename T>
struct rpc_type<T *> {
    using type = typed_object_handle_s<T>;
};

template<typename T>
using rpc_type_t = typename rpc_type<T>::type;

} // namespace reflex::rpc


namespace reflex::config {

template<typename T>
struct custom_traits<rpc::typed_object_handle_s<T>> {
    static expected_t<boost::json::value> try_serialize(Context &context,
                                                        const context_entry_s &entry,
                                                        const rpc::object_handle_s &value,
                                                        const boost::json::storage_ptr &memory_resource) {
        return try_serialize_value(context, entry, value, memory_resource);
    }

    static expected_t<rpc::typed_object_handle_s<T>> try_deserialize(DeserializationContext &context,
                                                                     const context_entry_s &entry,
                                                                     const boost::json::value &value) {
        if (auto exp_untyped = try_deserialize_value<rpc::object_handle_s>(context, entry, value)) {
            return expected_t<rpc::typed_object_handle_s<T>>{std::in_place, *std::move(exp_untyped)};
        } else {
            return std::unexpected{std::move(exp_untyped).error()};
        }
    }

    static expected_t<void> try_validate(Context & /*context*/,
                                         const context_entry_s & /*entry*/,
                                         const rpc::object_handle_s & /*value*/) noexcept {
        return {};
    }
};

template<typename T>
struct custom_traits<std::shared_ptr<T>> {
    using Pointer = std::shared_ptr<T>;

    static expected_t<boost::json::value> try_serialize(Context &context,
                                                        const context_entry_s &entry,
                                                        const Pointer &value,
                                                        const boost::json::storage_ptr &memory_resource) {
        const auto &ctx = static_cast<rpc::RpcSerializationContext &>(context);
        auto &handle_storage = ctx.handle_storage;

        auto *const raw_ptr = value ? std::addressof(*value) : nullptr; // TODO const correctness
        const auto handle = handle_storage.add_handle(raw_ptr, value, reflex::type_name<std::remove_const_t<T>>);
        return config::try_serialize_value(context, entry, handle, memory_resource);
    }

    static expected_t<Pointer> try_deserialize(DeserializationContext &context,
                                               const context_entry_s &entry,
                                               const boost::json::value &value) {
        if (value.is_null()) {
            return Pointer{};
        }

        if (auto exp_handle_info = config::try_deserialize_value<rpc::object_handle_s>(context, entry, value)) {
            auto &ctx = static_cast<rpc::RpcDeserializationContext &>(context);
            auto &handle_storage = ctx.handle_storage; // TODO const correctness
            auto pointer = handle_storage.get_pointer(*exp_handle_info, reflex::type_name<std::remove_const_t<T>>);

            return std::static_pointer_cast<T>(std::move(pointer.smart_ptr));
        } else {
            return std::unexpected{std::move(exp_handle_info).error()};
        }
    }

    static expected_t<void> try_validate(Context & /*context*/,
                                         const context_entry_s & /*entry*/,
                                         const Pointer & /*value*/) {
        return {};
    }
};

template<typename T>
struct custom_traits<T *> {
    using Pointer = T *;

    [[nodiscard]] static expected_t<boost::json::value> try_serialize(
            Context & /* context */,
            const context_entry_s & /*entry*/,
            const Pointer & /*value*/,
            const boost::json::storage_ptr & /*memory_resource*/) {
        static_assert(false, "Serialization of raw pointers is not supported");
        return {};
    }

    static expected_t<Pointer> try_deserialize(DeserializationContext &context,
                                               const context_entry_s &entry,
                                               const boost::json::value &value) {
        if (value.is_null()) {
            return Pointer{};
        }

        if (auto exp_handle_info = config::try_deserialize_value<rpc::object_handle_s>(context, entry, value)) {
            auto &ctx = static_cast<rpc::RpcDeserializationContext &>(context);
            auto &handle_storage = ctx.handle_storage; // TODO const correctness
            auto pointer = handle_storage.get_pointer(*exp_handle_info, reflex::type_name<std::remove_const_t<T>>);

            return static_cast<Pointer>(pointer.raw_ptr);
        } else {
            return std::unexpected{std::move(exp_handle_info).error()};
        }
    }

    static expected_t<void> try_validate(Context & /*context*/, const context_entry_s & /*entry*/, Pointer /*value*/) {
        return {};
    }
};

} // namespace reflex::config