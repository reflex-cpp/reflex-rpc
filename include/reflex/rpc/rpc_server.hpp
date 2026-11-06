// Part of the Reflex project.
// Copyright © Reflex Project Contributors.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>

#include <boost/json/object.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>
#include <boost/mp11/algorithm.hpp>
#include <unifex/any_scheduler.hpp>
#include <unifex/async_scope.hpp>
#include <unifex/defer.hpp>
#include <unifex/just.hpp>
#include <unifex/just_from.hpp>
#include <unifex/let_error.hpp>
#include <unifex/spawn_detached.hpp>
#include <unifex/then.hpp>

#include <message_queue/message_queue.hpp>
#include <reflex/config/deserialize.hpp>
#include <reflex/config/serialize.hpp>
#include <reflex/function_traits.hpp>
#include <reflex/meta.hpp>

#include <reflex/rpc/common.hpp>

namespace reflex::rpc {

using message_queue::MessageQueue;

class RpcServer {
public:
    struct rpc_input_message {
        std::string fn_name;
        boost::json::array args;
        MessageQueue::message_s msg;
        std::vector<MessageQueue::message_s::data_span_t> blob_spans;
        InputBinaryBlobStorage blob_storage;

        explicit rpc_input_message(MessageQueue::message_s &&msg_);

        rpc_input_message(rpc_input_message &&rhs) noexcept = default;
        rpc_input_message &operator=(rpc_input_message &&rhs) noexcept = default;

        rpc_input_message(const rpc_input_message &rhs) = delete;
        rpc_input_message &operator=(const rpc_input_message &rhs) = delete;
    };
    using rpc_callback_t = std::function<void(MessageQueue::message_s::id_t, rpc_input_message &&)>;

    explicit RpcServer(unifex::any_scheduler scheduler, std::unique_ptr<MessageQueue> message_queue);
    ~RpcServer();

    void message_handler(MessageQueue::message_s msg);
    void broadcast_message(MessageQueue::message_s msg) const;
    void send_message(MessageQueue::message_s msg) const;


    std::vector<std::string> registered_functions();

    template<typename T>
    [[nodiscard]] std::remove_cvref_t<T> parse_argument(const boost::json::value &value,
                                                        const InputBinaryBlobStorage &blob_storage);

    template<auto &Function, typename Context = std::nullopt_t>
    void register_function(Context context = std::nullopt);

    template<auto &Function, typename Context = std::nullopt_t>
    void register_function(std::string_view name, Context context = std::nullopt);

    template<typename Class>
    void register_class();

    template<typename CallFunction>
    auto register_function_impl(std::string_view name, CallFunction &&call_function);

    void shut_down();

    auto get_last_client_id() const noexcept { return m_last_client_id.load(); }
    auto get_client_count() const noexcept { return m_connected_client_count.load(); }

    HandleStorage &get_handle_storage() { return m_handle_storage; }
    auto get_message_queue() const { return m_message_queue.get(); }

private:
    using message_id_t = MessageQueue::message_s::id_t;

    unifex::async_scope m_async_scope;
    unifex::any_scheduler m_scheduler;
    std::atomic<std::uint32_t> m_last_client_id = 1;
    std::atomic<std::uint32_t> m_connected_client_count = 0;
    std::unique_ptr<MessageQueue> m_message_queue;
    std::atomic<bool> m_shutting_down = false;
    std::mutex m_registered_functions_mutex{};
    std::map<std::string, rpc_callback_t, std::less<>> m_registered_functions;
    HandleStorage m_handle_storage;

    void register_function_raw(const std::string &name, rpc_callback_t callback);

    template<auto &Function>
    auto call_function(rpc_input_message &&msg) {
        using meta_t = reflex::func_meta<Function>;

        constexpr auto argument_count = std::tuple_size_v<decltype(meta_t::parameters)>;

        return [this, msg = std::move(msg)]<std::size_t... I>(std::index_sequence<I...> /* member_indices */) mutable {
            if constexpr (unifex::sender<typename reflex::function_traits<Function>::result_type>) {
                return unifex::defer([msg = std::move(msg), this]() mutable {
                    (void)this; // suppress warning for unused variable for sizeof...(I) == 0
                    return Function(
                            parse_argument<typename std::tuple_element_t<I, decltype(meta_t::parameters)>::type>(
                                    msg.args[I],
                                    msg.blob_storage)...);
                });
            } else {
                return unifex::just_from([msg = std::move(msg), this]() mutable {
                    (void)this; // suppress warning for unused variable for sizeof...(I) == 0
                    return Function(
                            parse_argument<typename std::tuple_element_t<I, decltype(meta_t::parameters)>::type>(
                                    msg.args[I],
                                    msg.blob_storage)...);
                });
            }
        }(std::make_index_sequence<argument_count>{});
    }

    template<auto &Function, typename Context>
    auto call_function_with_context(const Context &context, rpc_input_message &&msg) {
        using meta_t = reflex::func_meta<Function>;

        constexpr auto parameter_count = std::tuple_size_v<decltype(meta_t::parameters)>;
        static_assert(parameter_count > 0, "Function should accept context");

        static_assert(std::is_same_v<
                              std::remove_cvref_t<typename std::tuple_element_t<0, decltype(meta_t::parameters)>::type>,
                              std::remove_cvref_t<Context>>,
                      "Invalid context type");

        return [this, msg = std::move(msg), context]<std::size_t... I>(
                       std::index_sequence<I...> /* member_indices */) mutable {
            if constexpr (unifex::sender<typename reflex::function_traits<Function>::result_type>) {
                return unifex::defer([msg = std::move(msg), this, context]() mutable {
                    (void)this; // suppress warning for unused variable for sizeof...(I) == 0
                    return Function(
                            context,
                            parse_argument<typename std::tuple_element_t<I + 1, decltype(meta_t::parameters)>::type>(
                                    msg.args[I],
                                    msg.blob_storage)...);
                });
            } else {
                return unifex::just_from([msg = std::move(msg), this, context]() mutable {
                    (void)this; // suppress warning for unused variable for sizeof...(I) == 0
                    return Function(
                            context,
                            parse_argument<typename std::tuple_element_t<I + 1, decltype(meta_t::parameters)>::type>(
                                    msg.args[I],
                                    msg.blob_storage)...);
                });
            }
        }(std::make_index_sequence<parameter_count - 1>{});
    }

    template<typename Class, typename meta_t>
    auto call_member_function(rpc_input_message &&msg) {
        constexpr auto argument_count = std::tuple_size_v<decltype(meta_t::parameters)>;

        if (msg.args.size() < 1) {
            throw std::runtime_error("A member function cannot be called without the self argument");
        }

        using self_t = Class *;
        auto self = parse_argument<self_t>(msg.args[0], msg.blob_storage);

        return [this, msg = std::move(msg), self]<std::size_t... I>(
                       std::index_sequence<I...> /* member_indices */) mutable {
            (void)this; // suppress warning for unused variable for sizeof...(I) == 0
            return ((*self).*
                    (meta_t::ptr))(parse_argument<typename std::tuple_element_t<I, decltype(meta_t::parameters)>::type>(
                    msg.args[I + 1],
                    msg.blob_storage)...);
        }(std::make_index_sequence<argument_count>{});
    }

    template<typename... Values>
    [[nodiscard]] MessageQueue::message_s results_to_message_impl(const message_id_t message_id, Values &&...results) {
        if constexpr (sizeof...(results) == 0) {
            return MessageQueue::message_s::from_str(
                    message_id,
                    boost::json::serialize(boost::json::object{{"result", boost::json::value{}}}));
        } else if constexpr (sizeof...(results) == 1) {
            OutputBinaryBlobStorage blob_storage;
            RpcSerializationContext context{m_handle_storage, blob_storage};
            const auto result_str = boost::json::serialize(
                    boost::json::object{
                            {"result", config::serialize_value(context, {}, std::forward<Values>(results)..., {})}},
                    {.allow_infinity_and_nan = true});
            std::vector<MessageQueue::message_s::data_span_t> datas;
            datas.reserve(blob_storage.get_blobs().size() + 1);
            datas.emplace_back(std::as_bytes(std::span{result_str}));
            for (const auto &blob : blob_storage.get_blobs()) {
                datas.emplace_back(blob);
            }
            return MessageQueue::message_s(message_id, datas);
        } else {
            static_assert(false, "Multiple results are not supported");
        }
    }

    [[nodiscard]] auto results_to_message(const message_id_t message_id) noexcept {
        return [this, message_id]<typename... Values>(Values &&...results) {
            return results_to_message_impl(message_id, std::forward<Values>(results)...);
        };
    }

    using error_to_message_result_t = decltype(unifex::just(std::declval<MessageQueue::message_s>()));
    static error_to_message_result_t error_to_message_impl(const std::exception_ptr &ex, const message_id_t message_id);

    [[nodiscard]] auto send_response() {
        return [this](MessageQueue::message_s msg) { send_message(std::move(msg)); };
    }
};

template<typename T>
[[nodiscard]] std::remove_cvref_t<T> RpcServer::parse_argument(const boost::json::value &value,
                                                               const InputBinaryBlobStorage &blob_storage) {
    auto context = RpcDeserializationContext{m_handle_storage, blob_storage};
    return config::deserialize_value<std::remove_cv_t<std::remove_reference_t<T>>>(context, {}, value);
}

template<auto &Function, typename Context>
void RpcServer::register_function(Context context) {
    using meta_t = reflex::func_meta<Function>;
    register_function_impl(meta_t::full_name, [this, context](rpc_input_message &&msg) {
        if constexpr (!std::is_same_v<std::nullopt_t, std::remove_cvref_t<Context>>) {
            return call_function_with_context<Function>(context, std::move(msg));
        } else {
            (void)context; // suppress unused variable warning
            return call_function<Function>(std::move(msg));
        }
    });
};

template<auto &Function, typename Context>
void RpcServer::register_function(const std::string_view name, Context context) {
    register_function_impl(name, [this, context](rpc_input_message &&msg) {
        if constexpr (!std::is_same_v<std::nullopt_t, std::remove_cvref_t<Context>>) {
            return call_function_with_context<Function>(context, std::move(msg));
        } else {
            (void)context; // suppress unused variable warning
            return call_function<Function>(std::move(msg));
        }
    });
};


template<typename Class>
void RpcServer::register_class() {
    using meta_t = reflex::meta<Class>;
    boost::mp11::mp_for_each<boost::mp11::mp_transform<boost::mp11::mp_identity, typename meta_t::member_functions_t>>(
            [this]<typename MemFuncInfo>(boost::mp11::mp_identity<MemFuncInfo>) {
                register_function_impl(MemFuncInfo::full_name, [&](rpc_input_message &&msg) {
                    using call_result =
                            std::invoke_result_t<decltype(&RpcServer::call_member_function<Class, MemFuncInfo>),
                                                 RpcServer *,
                                                 rpc_input_message &&>;

                    if constexpr (unifex::sender<call_result>) {
                        return call_member_function<Class, MemFuncInfo>(std::move(msg));
                    } else {
                        return unifex::just_from([msg = std::move(msg), this]() mutable {
                            return call_member_function<Class, MemFuncInfo>(std::move(msg));
                        });
                    }
                });
            });
}

template<typename CallFunction>
auto RpcServer::register_function_impl(const std::string_view name, CallFunction &&call_function) {
    register_function_raw(
            std::string{name},
            [this, call_function = std::forward<CallFunction>(call_function)](const message_id_t message_id,
                                                                              rpc_input_message &&msg) {
                unifex::spawn_detached(
                        unifex::on(m_scheduler,
                                   call_function(std::move(msg)) | unifex::then(results_to_message(message_id)) |
                                           unifex::let_error([message_id](const std::exception_ptr &ex) {
                                               return error_to_message_impl(ex, message_id);
                                           }) |
                                           unifex::then(send_response())),
                        m_async_scope);
            });
}


} // namespace reflex::rpc
