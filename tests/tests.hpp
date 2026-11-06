// Part of the Reflex project.
// Copyright © Reflex Project Contributors.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <unifex/any_sender_of.hpp>
#include <unifex/just.hpp>
#include <unifex/just_from.hpp>

#include <reflex/rpc/binary_blob.hpp>

namespace reflex::rpc::test {

[[reflex::reflect]] inline std::array<double, 3> get_nan() {
    static constexpr std::array specific_doubles = {
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(),
    };
    return specific_doubles;
}

[[reflex::reflect]] inline std::int32_t get_answer() { return 42; }
[[reflex::reflect]] inline unifex::any_sender_of<std::int32_t> get_async_answer() { return unifex::just(42); }

[[reflex::reflect]] inline void do_nothing() {}
[[reflex::reflect]] inline unifex::any_sender_of<> do_nothing_async() { return unifex::just(); }

[[reflex::reflect]] inline std::vector<double> get_numbers_back(std::vector<double> numbers) { return numbers; }
[[reflex::reflect]] inline binary_blob_s get_blob_back(binary_blob_s data) { return data; }

[[reflex::reflect]] inline std::string append_number(const std::string str, const std::int32_t num) {
    return str + " " + std::to_string(num);
}

[[reflex::reflect]] void wait_for_disconnect();

[[reflex::reflect]] inline void throw_error() { throw std::runtime_error{"some error"}; }

class [[reflex::reflect]] TestClass {
public:
    auto get_zero() const { return 0; }

    auto trivial_method() {
        m_value += 42;
        return m_value;
    }

    auto get_number() const noexcept { return 13; }

    auto sum(std::int32_t a, std::int32_t b) const noexcept { return a + b; }

private:
    std::int32_t m_value = 0;
};

[[reflex::reflect]] inline std::shared_ptr<TestClass> create_test_class() { return std::make_shared<TestClass>(); }


struct non_reflected_complex_object_s {
    int value;
};

struct non_deserializable_value_s {
    explicit non_deserializable_value_s(const int init_value) : value{init_value} {}
    int value;
};

[[reflex::reflect]] inline std::shared_ptr<non_reflected_complex_object_s> create_complex_object() {
    return std::make_shared<non_reflected_complex_object_s>(non_reflected_complex_object_s{.value = 666});
}

[[reflex::reflect]] inline unifex::any_sender_of<int> complex_object_get_value(
        std::shared_ptr<non_reflected_complex_object_s> object) {
    return unifex::just_from([object = std::move(object)] { return object->value; });
}

} // namespace reflex::rpc::test

#ifndef __REFLEX_TOOL__
    #include "tests.meta.inc"
#endif
