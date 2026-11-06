// Part of the Reflex project.
// Copyright © Reflex Project Contributors.
// SPDX-License-Identifier: Apache-2.0

#include "test_context.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <random>
#include <stdexcept>

#include <spdlog/spdlog.h>
#include <unifex/inline_scheduler.hpp>

#include <message_queue/message_queue.hpp>

namespace reflex::rpc::test {

TestContext::TestContext(const test_options_s options)
    : thread_pool{options.thread_count},
      timeout_exit_thread{[this] {
          // TODO remove when cancellation support will be added to RPC client
          if (!finished.try_acquire_for(std::chrono::seconds{10})) {
              spdlog::error("Test timeout exceeded");
              spdlog::default_logger()->flush();
              std::quick_exit(1);
          }
      }} {
    try {
        auto message_queue = MessageQueue::create_socket_message_queue(spdlog::default_logger());
        message_queue->set_event_handler([this](const auto event) {
            switch (event) {
                case MessageQueue::event_e::server_started: {
                    rpc_server_started.release();
                    break;
                }
                case MessageQueue::event_e::server_stopped: {
                    break;
                }
                default: break;
            }
        });
        message_queue->start_server(0);
        if (!rpc_server_started.try_acquire_for(std::chrono::seconds{3})) {
            stop_and_throw("RPC server failed to start in expected timeout");
        }
        const auto port = message_queue->port();

        server = std::make_unique<RpcServer>(unifex::inline_scheduler{}, std::move(message_queue));

        client.set_event_callback([this, options](const auto &event) {
            const auto expected_connected_event_type = options.use_shared_memory
                                                               ? MessageQueue::event_e::shared_memory_created
                                                               : MessageQueue::event_e::client_connected;
            if (event == expected_connected_event_type) {
                m_connected.release();
            }

            spdlog::info("Client event: {}", std::to_underlying(event));
        });

        client.try_connect("localhost",
                           port,
                           options.use_shared_memory
                                   ? std::optional<MessageQueue::shared_memory_config_s>({.max_num_msg_blocks = 2})
                                   : std::optional<MessageQueue::shared_memory_config_s>());
        wait_for_connect();
    } catch (const std::exception &) {
        stop = true;
        throw;
    }
}

void TestContext::stop_and_throw(const std::string &message) {
    stop = true;
    throw std::runtime_error{message};
}

void TestContext::wait_for_connect() {
    if (!m_connected.try_acquire_for(std::chrono::seconds{5})) {
        throw std::runtime_error("RPC client failed to establish connection");
    }
}

TestContext::~TestContext() {
    finished.release();
    stop = true;
}

} // namespace reflex::rpc::test