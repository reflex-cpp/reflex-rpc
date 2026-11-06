// Part of the Reflex project.
// Copyright © Reflex Project Contributors.
// SPDX-License-Identifier: Apache-2.0

#include <reflex/rpc/rpc_server.hpp>

#include <ranges>

#include <boost/json/parse.hpp>
#include <unifex/sync_wait.hpp>

#include <reflex/config/detail/throw_formatted.hpp>
#include <reflex/ranges/to.hpp>

namespace reflex::rpc {

using message_queue::MessageQueue;

RpcServer::RpcServer(unifex::any_scheduler scheduler, std::unique_ptr<MessageQueue> message_queue)
    : m_scheduler{std::move(scheduler)},
      m_message_queue(std::move(message_queue)) {
    m_message_queue->set_message_handler([this](MessageQueue::message_s msg) { message_handler(std::move(msg)); });
    m_message_queue->set_event_handler([this](const MessageQueue::event_e event) {
        switch (event) {
            case MessageQueue::event_e::server_started: break;
            case MessageQueue::event_e::client_started: break;
            case MessageQueue::event_e::server_stopped: break;
            case MessageQueue::event_e::client_stopped: break;
            case MessageQueue::event_e::client_connected: {
                ++m_connected_client_count;
                break;
            }
            case MessageQueue::event_e::client_cant_connect: break;
            case MessageQueue::event_e::client_disconnected: {
                --m_connected_client_count;
                break;
            }
            case MessageQueue::event_e::shared_memory_created: break;
        }
    });

    auto context = rpc_server_ctx_s{.rpc_server = *this};
    register_function<reset_object_handle>(context);
    register_function<get_registered_functions>(context);
}

RpcServer::~RpcServer() {
    {
        shut_down();

        m_async_scope.request_stop();
        unifex::sync_wait(m_async_scope.complete());
    }

    m_message_queue->stop();
}

RpcServer::rpc_input_message::rpc_input_message(MessageQueue::message_s &&msg_) : msg{std::move(msg_)} {
    blob_spans = msg.get_datas();
    const auto input_json = msg.get_str();
    const auto input_blobs = std::span{blob_spans.begin() + 1, blob_spans.end()};
    blob_storage = InputBinaryBlobStorage{input_blobs};
    auto json = boost::json::parse(input_json,
                                   {},
                                   boost::json::parse_options{.max_depth = 256,
                                                              .numbers = boost::json::number_precision::precise,
                                                              .allow_infinity_and_nan = true});
    auto &parsed_msg = json.as_object();
    fn_name = parsed_msg.at("fn").as_string();
    args = std::move(parsed_msg.at("args").as_array());
}

void RpcServer::message_handler(MessageQueue::message_s msg) {
    if (m_shutting_down) {
        return; // ignore new messages when shutting down
    }

    const auto message_id = msg.get_id();

    m_last_client_id = message_id.client_id;

    try {
        rpc_input_message parsed_message(std::move(msg));

        const rpc_callback_t *function = nullptr;
        {
            const auto guard = std::lock_guard{m_registered_functions_mutex};
            if (const auto it = m_registered_functions.find(parsed_message.fn_name);
                it != m_registered_functions.end()) {
                function = &it->second;
            }
        }
        if (!function) {
            config::detail::throw_formatted<Error>("Invalid rpc function name: {}", parsed_message.fn_name);
        }
        (*function)(message_id, std::move(parsed_message));
    } catch (const std::exception &e) {
        auto error_message = MessageQueue::message_s::from_str(
                message_id,
                boost::json::serialize(boost::json::object{{"error", e.what()}}));
        send_message(std::move(error_message));
    }
}


void RpcServer::broadcast_message(MessageQueue::message_s msg) const {
    m_message_queue->broadcast_message(std::move(msg));
}
void RpcServer::send_message(MessageQueue::message_s msg) const { m_message_queue->send_message(std::move(msg)); }

void RpcServer::register_function_raw(const std::string &name, rpc_callback_t callback) {
    const auto guard = std::lock_guard{m_registered_functions_mutex};
    if (!m_registered_functions.try_emplace(name, std::move(callback)).second) {
        config::detail::throw_formatted<Error>("function with name {} is already registered", name);
    }
}

std::vector<std::string> RpcServer::registered_functions() {
    const auto guard = std::lock_guard{m_registered_functions_mutex};
    return std::views::keys(m_registered_functions) | reflex::ranges::to<std::vector>();
}

RpcServer::error_to_message_result_t RpcServer::error_to_message_impl(const std::exception_ptr &ex,
                                                                      const message_id_t message_id) {
    try {
        std::rethrow_exception(ex);
    } catch (const std::exception &e) {
        return unifex::just(
                MessageQueue::message_s::from_str(message_id,
                                                  boost::json::serialize(boost::json::object{{"error", e.what()}})));
    } catch (...) {
        return unifex::just(MessageQueue::message_s::from_str(
                message_id,
                boost::json::serialize(boost::json::object{{"error", "unknown error"}})));
    }
}

void RpcServer::shut_down() { m_shutting_down.store(true); }


} // namespace reflex::rpc