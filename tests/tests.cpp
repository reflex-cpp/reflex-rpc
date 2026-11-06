// Part of the Reflex project.
// Copyright © Reflex Project Contributors.
// SPDX-License-Identifier: Apache-2.0

#include "tests.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <semaphore>
#include <span>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <reflex/rpc/handle.hpp>

#include "test_context.hpp"

namespace reflex::rpc::test {

TEST_CASE("rpc::RpcServer get_nan using shared memory") {
    auto ctx = TestContext{{.use_shared_memory = true}};
    ctx.server->register_function<get_nan>();

    const auto result = ctx.sync_wait(ctx.client.call_function<get_nan>());

    REQUIRE(result.has_value());
    REQUIRE(std::isnan(result.value()[0]));
    REQUIRE(std::isinf(result.value()[1]));
    REQUIRE(std::isinf(result.value()[2]));
}

TEST_CASE("rpc::RpcServer get_numbers_back") {
    auto ctx = TestContext{};
    ctx.server->register_function<get_numbers_back>();

    const auto input = std::vector{1.0, 2.0, 3.0};
    const auto result = ctx.sync_wait(ctx.client.call_function<get_numbers_back>(input));

    REQUIRE(result.has_value());
    REQUIRE(result.value() == input);
}

TEST_CASE("rpc::RpcServer create_complex_object") {
    auto ctx = TestContext{};
    ctx.server->register_function<create_complex_object>();

    const auto result = ctx.sync_wait(ctx.client.call_function<create_complex_object>());
    REQUIRE(result.has_value());
    const auto &handle = result.value();

    REQUIRE(handle.type == reflex::type_name<non_reflected_complex_object_s>);
}

TEST_CASE("rpc::RpcServer complex_object_get_value") {
    auto ctx = TestContext{};
    ctx.server->register_function<create_complex_object>();
    ctx.server->register_function<complex_object_get_value>();

    const auto handle = ctx.sync_wait(ctx.client.call_function<create_complex_object>());
    REQUIRE(handle.has_value());

    const auto result = ctx.sync_wait(ctx.client.call_function<complex_object_get_value>(handle.value()));

    REQUIRE(result.has_value());
    REQUIRE(result.value() == 666);
}

TEST_CASE("rpc::RpcServer reset_object_handle") {
    auto ctx = TestContext{};
    ctx.server->register_function<create_complex_object>();

    const auto handle = ctx.sync_wait(ctx.client.call_function<create_complex_object>());
    REQUIRE_NOTHROW(ctx.sync_wait(ctx.client.call_function<reset_object_handle>(handle.value())));
}

TEST_CASE("rpc::RpcServer throw_error") {
    auto ctx = TestContext{};
    ctx.server->register_function<throw_error>();

    REQUIRE_THROWS_AS(ctx.sync_wait(ctx.client.call_function<throw_error>()), std::runtime_error);
    REQUIRE_THROWS_WITH(ctx.sync_wait(ctx.client.call_function<throw_error>()),
                        Catch::Matchers::ContainsSubstring("some error"));
}

TEST_CASE("rpc::RpcServer create_test_class") {
    auto ctx = TestContext{};
    ctx.server->register_function<create_test_class>();

    const auto result = ctx.sync_wait(ctx.client.call_function<create_test_class>());
    REQUIRE(result.has_value());
}

TEST_CASE("rpc::RpcServer TestClass.trivial_method") {
    auto ctx = TestContext{};
    ctx.server->register_class<TestClass>();
    ctx.server->register_function<create_test_class>();

    const auto handle = ctx.sync_wait(ctx.client.call_function<create_test_class>());
    REQUIRE(handle.has_value());

    const auto result = ctx.sync_wait(ctx.client.call_member_function<&TestClass::trivial_method>(handle.value()));

    REQUIRE(result.has_value());
    REQUIRE(result.value() == 42);

    const auto second_result = ctx.sync_wait(
            ctx.client.call_member_function<&TestClass::trivial_method>(handle.value()));

    REQUIRE(second_result.has_value());
    REQUIRE(second_result.value() == 84);

    const auto sum = ctx.sync_wait(ctx.client.call_member_function<&TestClass::sum>(handle.value(), 1, 2));

    REQUIRE(sum.has_value());
    REQUIRE(sum.value() == 1 + 2);
}

TEST_CASE("rpc::RpcClient call function get_answer") {
    auto ctx = TestContext{};

    ctx.server->register_function<get_answer>();

    const auto result = ctx.sync_wait(ctx.client.call_function<get_answer>());

    REQUIRE(result.has_value());
    REQUIRE(result.value() == 42);
}

TEST_CASE("rpc::RpcClient call async functions") {
    auto ctx = TestContext{};
    ctx.server->register_function<get_async_answer>();
    ctx.server->register_function<do_nothing>();
    ctx.server->register_function<do_nothing_async>();

    const auto result = ctx.sync_wait(ctx.client.call_function<get_async_answer>());
    REQUIRE(result.has_value());
    REQUIRE(result.value() == 42);

    REQUIRE_NOTHROW(ctx.sync_wait(ctx.client.call_function<do_nothing_async>()));
    REQUIRE_NOTHROW(ctx.sync_wait(ctx.client.call_function<do_nothing>()));
}

TEST_CASE("rpc::RpcClient get blob back") {
    auto ctx = TestContext{};
    ctx.server->register_function<get_blob_back>();

    const std::string binary_data = "test string";
    const auto data_span = std::as_bytes(std::span(binary_data));

    binary_blob_s blob{};
    blob.bytes.resize(data_span.size_bytes());
    std::memcpy(blob.bytes.data(), data_span.data(), data_span.size_bytes());

    const auto result = ctx.sync_wait(ctx.client.call_function<get_blob_back>(blob));

    REQUIRE(result.has_value());
    REQUIRE(result.value().bytes == blob.bytes);
}

TEST_CASE("rpc::RpcClient multiple arguments") {
    auto ctx = TestContext{};
    ctx.server->register_function<append_number>();

    const auto result = ctx.sync_wait(ctx.client.call_function<append_number>("Peugeot", 407));

    REQUIRE(result.has_value());
    REQUIRE(result.value() == "Peugeot 407");
}

namespace {

std::binary_semaphore ready_to_disconnect{0};
std::binary_semaphore disconnect_called{0};

} // namespace

void wait_for_disconnect() {
    ready_to_disconnect.release();
    if (!disconnect_called.try_acquire_for(std::chrono::milliseconds(3000))) {
        throw std::runtime_error("not disconnected");
    }
}

TEST_CASE("rpc::RpcClient disconnect") {
    auto ctx = TestContext{};
    ctx.server->register_function<wait_for_disconnect>();

    std::jthread disconnect_thread([&] {
        if (!ready_to_disconnect.try_acquire_for(std::chrono::milliseconds(3000))) {
            return;
        }
        ctx.client.disconnect();
        disconnect_called.release();
    });
    REQUIRE_THROWS_WITH(ctx.sync_wait(ctx.client.call_function<wait_for_disconnect>()),
                        Catch::Matchers::ContainsSubstring("disconnected"));
}

} // namespace reflex::rpc::test