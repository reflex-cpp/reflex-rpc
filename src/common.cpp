// Part of the Reflex project.
// Copyright © Reflex Project Contributors.
// SPDX-License-Identifier: Apache-2.0

#include <reflex/rpc/common.hpp>

#include <algorithm>
#include <ranges>

namespace reflex::rpc {

Error::Error(const char *const message) : runtime_error{message} {}

void HandleStorage::reset_handle(const object_handle_s &handle) {
    auto ptr = std::shared_ptr<void>{};
    {
        const auto guard = std::lock_guard{m_mutex};

        if (m_handles.size() <= handle.id) {
            config::detail::throw_formatted<Error>("Invalid handle id={}. Id is bigger than handle count ({})",
                                                   handle.id,
                                                   m_handles.size());
        }
        auto &pointer = m_handles[handle.id];

        if (pointer.generation != handle.generation) {
            config::detail::throw_formatted<Error>(
                    "Unable to reset handle id={}. Generation is different: given {}, existing: {}",
                    handle.id,
                    handle.generation,
                    pointer.generation);
        }

        pointer.raw_ptr = nullptr;
        ptr = std::move(pointer.smart_ptr);
        pointer.generation += 1;
        m_free_indices.push_back(handle.id);
    }
    // The pointer is reset outside the lock to prevent deadlock in case the destructor of the stored object relies on
    // code that deletes or creates another handle
    ptr.reset();
}

object_handle_s HandleStorage::add_handle(void *raw_ptr,
                                          std::shared_ptr<void> smart_ptr,
                                          const std::string_view handle_type_name) {
    const auto guard = std::lock_guard{m_mutex};

    if (m_free_indices.empty()) {
        reserve_handles();
    }

    const auto handle_id = m_free_indices.back();
    m_free_indices.pop_back();

    auto &handle = m_handles[handle_id];
    handle.raw_ptr = raw_ptr;
    handle.smart_ptr = std::move(smart_ptr);
    handle.global_idx = ++m_global_idx;

    return {.id = handle_id, .type = std::string{handle_type_name}, .generation = handle.generation};
}

object_ptr_s HandleStorage::get_pointer(const object_handle_s &handle, const std::string_view type_name) const {
    const auto guard = std::lock_guard{m_mutex};

    if (m_handles.size() <= handle.id) {
        config::detail::throw_formatted<Error>("Invalid handle id={}. Id is bigger than handle count ({})",
                                               handle.id,
                                               m_handles.size());
    }

    auto &pointer = m_handles[handle.id];
    if (pointer.generation != handle.generation) {
        config::detail::throw_formatted<Error>("Invalid handle id={}. Generation is different: given {}, existing: {}",
                                               handle.id,
                                               handle.generation,
                                               pointer.generation);
    }

    if (handle.type != type_name) {
        config::detail::throw_formatted<Error>("Invalid handle type. {} != {}", handle.type, type_name);
    }

    return pointer;
}

HandleStorage::~HandleStorage() {
    std::ranges::sort(m_handles,
                      [](const object_ptr_s &a, const object_ptr_s &b) { return a.global_idx > b.global_idx; });

    for (auto &handle : m_handles) {
        handle.raw_ptr = nullptr;
        handle.smart_ptr.reset();
    }
}

void HandleStorage::reserve_handles() {
    constexpr auto block_size = 512uz;
    const auto old_size = m_handles.size();
    m_handles.resize(old_size + block_size);
    for (const auto idx : std::views::iota(0uz, block_size) | std::views::reverse) {
        m_free_indices.push_back(old_size + idx);
    }
}

} // namespace reflex::rpc
