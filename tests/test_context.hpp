// Part of the Reflex project.
// Copyright © Reflex Project Contributors.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <semaphore>
#include <string>
#include <thread>
#include <utility>

#include <unifex/static_thread_pool.hpp>
#include <unifex/sync_wait.hpp>

#include <reflex/rpc/rpc_client.hpp>
#include <reflex/rpc/rpc_server.hpp>

namespace reflex::rpc::test {

struct test_options_s {
    bool use_shared_memory = false;
    std::uint32_t thread_count = std::thread::hardware_concurrency();
};

struct TestContext {
    std::binary_semaphore m_connected{0};
    std::binary_semaphore finished{0};
    std::atomic_bool stop = false;

    unifex::static_thread_pool thread_pool;

    std::binary_semaphore rpc_server_started{0};
    std::unique_ptr<RpcServer> server;

    RpcClient client{};

    std::jthread timeout_exit_thread;

    explicit TestContext(test_options_s options = {});

    ~TestContext();

    auto sync_wait(auto &&sender) {
        // TODO use this timed execution after cancellation support will be implemented in RPC client
        // unifex::timed_single_thread_context timed_context{};
        // return unifex::sync_wait(
        //         unifex::stop_when(std::move(sender),
        //                           unifex::schedule_after(timed_context.get_scheduler(), std::chrono::seconds{10})) |
        //         unifex::let_done([] {
        //             return unifex::just_error(std::make_exception_ptr(std::runtime_error{"Test timeout"}));
        //         }));

        return unifex::sync_wait(std::move(sender));
    }

    void stop_and_throw(const std::string &message);
    void wait_for_connect();
};


} // namespace reflex::rpc::test
