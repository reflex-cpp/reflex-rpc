// Part of the Reflex project.
// Copyright © Reflex Project Contributors.
// SPDX-License-Identifier: Apache-2.0

#include <reflex/rpc/handle.hpp>

#include <reflex/rpc/rpc_server.hpp>


namespace reflex::rpc {

void reset_object_handle(const rpc_server_ctx_s context, const object_handle_s handle) {
    context.rpc_server.get_handle_storage().reset_handle(handle);
}

std::vector<std::string> get_registered_functions(const rpc_server_ctx_s context) {
    return context.rpc_server.registered_functions();
}

} // namespace reflex::rpc