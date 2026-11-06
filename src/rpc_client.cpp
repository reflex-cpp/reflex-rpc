// Part of the Reflex project.
// Copyright © Reflex Project Contributors.
// SPDX-License-Identifier: Apache-2.0

#include <reflex/rpc/rpc_client.hpp>

#include <spdlog/spdlog.h>

namespace reflex::rpc {

RpcClient::RpcClient() : m_message_queue{MessageQueue::create_socket_message_queue(spdlog::default_logger())} {
    m_message_queue->set_event_handler([this](const auto &event) {
        if (event == MessageQueue::event_e::client_disconnected) {
            m_stopped = true;

            const auto guard = std::lock_guard{m_response_callbacks_mutex};
            for (auto &callback : m_response_callbacks | std::views::values) {
                callback.on_disconnect();
            }
            m_response_callbacks.clear();
        }

        if (m_event_callback) {
            m_event_callback(event);
        }
    });

    m_message_queue->set_message_handler([this](auto message) { on_message(message); });
}

void RpcClient::try_connect(const std::string_view host,
                            const std::uint16_t port,
                            std::optional<MessageQueue::shared_memory_config_s> shared_memory_config) {
    m_message_queue_thread = std::jthread(
            [port,
             host = std::string{host},
             shared_memory_config = std::move(shared_memory_config),
             &queue = m_message_queue] { queue->start_client(host, port, shared_memory_config); });
}

void RpcClient::set_event_callback(event_callback_t callback) { m_event_callback = std::move(callback); }


void RpcClient::disconnect() {
    if (!m_stopped.exchange(true)) {
        m_message_queue->stop();
    }
}

void RpcClient::on_message(const MessageQueue::message_s &msg) {
    spdlog::debug("CLIENT: id: {} {}", msg.get_id().msg_id, msg.get_str());

    const auto id = msg.get_id().msg_id;
    if (id < reserved_message_count) {
        return;
    }

    {
        const auto guard = std::lock_guard{m_response_callbacks_mutex};
        if (auto callback_it = m_response_callbacks.find(id); callback_it != m_response_callbacks.end()) {
            (callback_it->second.on_message)(msg);
            m_response_callbacks.erase(callback_it);
        } else {
            spdlog::error("Invalid response message id");
        }
    }
}

RpcClient::~RpcClient() {
    if (!m_stopped.exchange(true)) {
        m_message_queue->stop();
    }
}

} // namespace reflex::rpc
