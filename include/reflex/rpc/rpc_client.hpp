// Part of the Reflex project.
// Copyright © Reflex Project Contributors.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <semaphore>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

#include <boost/json/parse.hpp>
#include <boost/mp11/list.hpp>
#include <unifex/create.hpp>

#include <message_queue/message_queue.hpp>
#include <reflex/config/deserialize.hpp>
#include <reflex/config/serialize.hpp>
#include <reflex/function_traits.hpp>
#include <reflex/meta.hpp>

#include <reflex/rpc/common.hpp>
#include <reflex/rpc/message.hpp>

namespace reflex::rpc {

using message_queue::MessageQueue;

class RpcClient {
public:
    using event_callback_t = std::function<void(MessageQueue::event_e event)>;

    explicit RpcClient();
    ~RpcClient();

    void try_connect(std::string_view host,
                     std::uint16_t port,
                     std::optional<MessageQueue::shared_memory_config_s> shared_memory_config = std::nullopt);

    template<auto &Function, typename... Args>
    [[nodiscard]] auto call_function(Args &&...args);

    template<auto Function, typename... Args>
    [[nodiscard]] auto call_member_function(Args &&...args);

    template<typename FuncMeta, typename ReturnType, typename... Args>
    [[nodiscard]] auto call_function_impl(Args &&...args);

    void set_event_callback(event_callback_t callback);

    void disconnect();

private:
    void on_message(const MessageQueue::message_s &msg);

    template<typename T>
    [[nodiscard]] boost::json::value serialize_argument(const T &value, RpcSerializationContext &context);

    static constexpr auto reserved_message_count = 512;

    event_callback_t m_event_callback;
    std::atomic<decltype(MessageQueue::message_s::id_t::msg_id)> m_next_message_id = reserved_message_count + 1;

    HandleStorage m_handle_storage;

    std::unique_ptr<MessageQueue> m_message_queue;
    std::atomic_bool m_stopped = false;

    std::jthread m_message_queue_thread;

    using response_callback_t = std::function<void(const MessageQueue::message_s &response_message)>;
    using disconnect_callback_t = std::function<void()>;
    struct response_handlers_s {
        response_callback_t on_message;
        disconnect_callback_t on_disconnect;
    };

    std::mutex m_response_callbacks_mutex{};
    std::unordered_map<decltype(MessageQueue::message_s::id_t::msg_id), response_handlers_s> m_response_callbacks;
    // TODO replace with std::vector + index storage

    template<typename ResultType>
    auto make_response_cb(auto &rec);

    auto make_disconnect_cb(auto &rec);
};

template<auto &Function, typename... Args>
auto RpcClient::call_function(Args &&...args) {
    // TODO get args from function declaration instead of using template argument
    using meta_t = reflex::func_meta<Function>;
    using f_result_type = typename reflex::function_traits<Function>::result_type;
    return call_function_impl<meta_t, f_result_type, Args...>(std::forward<Args>(args)...);
}

template<auto Function, typename... Args>
auto RpcClient::call_member_function(Args &&...args) {
    using member_func_traits_t = reflex::member_function_traits<Function>;
    using func_meta_t = reflex::member_func_meta<Function>;

    using call_result = typename member_func_traits_t::result_type;
    return call_function_impl<func_meta_t, call_result, Args...>(std::forward<Args>(args)...);
}

namespace detail {

template<typename FuncMeta, typename... ParamInfo>
auto argument_types_with_self(const reflex::tuple<ParamInfo...> &) {
    if constexpr (detail::is_member_function<FuncMeta>) {
        using func_traits = reflex::member_function_traits<FuncMeta::ptr>;
        return boost::mp11::mp_list<typed_object_handle_s<typename func_traits::self_type>,
                                    typename ParamInfo::type...>{};
    } else {
        return boost::mp11::mp_list<typename ParamInfo::type...>{};
    }
}

} // namespace detail

template<typename FuncMeta, typename ReturnType, typename... Args>
auto RpcClient::call_function_impl(Args &&...args) {
    using namespace boost::mp11;

    constexpr auto argument_count = detail::function_argument_count<FuncMeta>();
    constexpr auto argument_offset = detail::function_argument_offset<FuncMeta>();
    static_assert(argument_count == sizeof...(Args), "Invalid argument count");

    static constexpr auto is_asynchronous = unifex::sender<ReturnType>;
    if constexpr (is_asynchronous) {
        using result_types = unifex::sender_value_types_t<ReturnType, mp_list, mp_list>;
        static constexpr auto result_count = mp_size<result_types>::value;
        static_assert(result_count == 1 || result_count == 0,
                      "Async functions with multiple return arguments are not supported");
    }

    using argument_types = decltype(detail::argument_types_with_self<FuncMeta>(FuncMeta::parameters));

    constexpr auto check_argument = []<std::size_t I> {
        using expected_argument_t = rpc_type_t<mp_at_c<argument_types, I + argument_offset>>;
        using given_argument_t = mp_at_c<mp_list<Args...>, I>;
        static_assert(std::is_convertible_v<given_argument_t, expected_argument_t>, "Invalid argument");
    };

    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (..., check_argument.template operator()<I>());
    }(std::make_index_sequence<argument_count>{});

    OutputBinaryBlobStorage blob_storage;
    RpcSerializationContext context = {m_handle_storage, blob_storage};
    const auto message_str = [&]<std::size_t... I>(std::index_sequence<I...> /* argument indices */) mutable {
        return config::serialize_config_to_string(
                call_message_s{.fn = std::string{FuncMeta::full_name},
                               .args = {serialize_argument<rpc_type_t<mp_at_c<argument_types, I + argument_offset>>>(
                                       std::forward<Args>(args),
                                       context)...}},
                {.allow_infinity_and_nan = true});
    }(std::make_index_sequence<argument_count>{});

    std::vector<MessageQueue::message_s::data_span_t> datas{};
    datas.reserve(blob_storage.get_blobs().size() + 1);
    datas.push_back(std::as_bytes(std::span{message_str}));
    for (const auto &blob : blob_storage.get_blobs()) {
        datas.emplace_back(blob);
    }

    auto message = MessageQueue::message_s{{.msg_id = ++m_next_message_id}, datas};

    using result_t = rpc_type_t<detail::return_type_t<ReturnType>>;
    using results_t = std::conditional_t<std::is_void_v<result_t>, mp_list<>, mp_list<result_t>>;
    return [&]<typename... Results>(mp_list<Results...>) {
        return unifex::create<Results...>([this, message = std::move(message)](auto &rec) mutable {
            {
                const auto guard = std::lock_guard{m_response_callbacks_mutex};
                m_response_callbacks.emplace(message.get_id().msg_id,
                                             response_handlers_s{.on_message = make_response_cb<result_t>(rec),
                                                                 .on_disconnect = make_disconnect_cb(rec)});
            }
            m_message_queue->send_message(std::move(message));
        });
    }(results_t{});
}

template<typename T>
boost::json::value RpcClient::serialize_argument(const T &value, RpcSerializationContext &context) {
    return config::serialize_value<std::remove_cv_t<std::remove_reference_t<T>>>(context, {}, value, {});
}

template<typename ResultType>
auto RpcClient::make_response_cb(auto &rec) {
    return [&rec, this](const MessageQueue::message_s &response_message) {
        try {
            const auto input_json = response_message.get_str();

            const auto blob_spans = response_message.get_datas();
            const auto input_blobs = std::span{blob_spans.begin() + 1, blob_spans.end()};
            const auto blob_storage = InputBinaryBlobStorage{input_blobs};
            RpcDeserializationContext context{m_handle_storage, blob_storage};

            const auto json = boost::json::parse(
                    input_json,
                    {},
                    boost::json::parse_options{.max_depth = 256,
                                               .numbers = boost::json::number_precision::precise,
                                               .allow_infinity_and_nan = true});

            const auto [error, result] = config::deserialize_value<response_message_s>(context, {}, json);

            if (error.has_value()) {
                unifex::set_error(std::move(rec), std::make_exception_ptr(std::runtime_error{error.value()}));
                return;
            }

            if constexpr (std::is_void_v<ResultType>) {
                unifex::set_value(std::move(rec));
            } else {
                unifex::set_value(std::move(rec), config::deserialize_value<ResultType>(context, {}, result));
            }
        } catch (...) {
            unifex::set_error(std::move(rec), std::current_exception());
        }
    };
}

auto RpcClient::make_disconnect_cb(auto &rec) {
    return [&rec] { unifex::set_error(std::move(rec), std::make_exception_ptr(std::runtime_error{"disconnected"})); };
}

} // namespace reflex::rpc