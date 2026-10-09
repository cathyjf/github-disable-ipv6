// SPDX-FileCopyrightText: Copyright 2026 Cathy J. Fitzpatrick <cathy@cathyjf.com>
// SPDX-License-Identifier: GPL-3.0-or-later

module;

#include <winsock2.h>
#include <netiodef.h>
#include <memory>
#include <wil/resource.h>

export module github_disable_ipv6.nsi;

import std;

export namespace github_disable_ipv6 {

// Query the IPv6 compartment IDs, including compartments other than the
// caller's current compartment.
auto GetNetworkCompartments() -> std::vector<NET_IF_COMPARTMENT_ID> {
    // These undocumented exports are also used by `netsh` to enumerate
    // compartments. Their signatures follow Wine's `include/wine/nsi.h`:
    // <https://github.com/wine-mirror/wine/blob/master/include/wine/nsi.h>.
    using AllocateTable = auto (WINAPI *)(DWORD, const NPI_MODULEID *, DWORD,
        void **, DWORD, void **, DWORD, void **, DWORD, void **, DWORD,
        DWORD *, DWORD) -> DWORD;
    using FreeTable = auto (WINAPI *)(void *, void *, void *, void *)
        noexcept -> void;

    const auto module = wil::unique_hmodule{LoadLibraryExA("nsi.dll", nullptr,
        LOAD_LIBRARY_SEARCH_SYSTEM32)};
    THROW_LAST_ERROR_IF_NULL_MSG(module, "could not load 'nsi.dll'");
    const auto load_function = [module_ = module.get()]<typename Function>(
        _In_z_ const char *const name) -> _Ret_notnull_ auto {
        [[gsl::suppress("26490", justification:
            "`GetProcAddress` returns the address of the requested function "
            "but with an incorrect return type.")]]
        const auto function = reinterpret_cast<Function>(
            GetProcAddress(module_, name));
        if (function != nullptr) {
            return function;
        }
        const auto transcoded_name = [name] {
            const auto last_error = wil::last_error_context{};
            return std::make_unique<std::wstring>(
                std::filesystem::path{name}.native());
        }();
        THROW_LAST_ERROR_MSG("could not resolve 'nsi.dll!%s'",
            transcoded_name->c_str());
    };
    const auto allocate = load_function.operator()<AllocateTable>(
        "NsiAllocateAndGetTable");
    // The `load_function` lambda never returns a null pointer.
    // The analyzer is unable to recognize that fact without this assumption.
    _Analysis_assume_(allocate != nullptr);
    const auto free = load_function.operator()<FreeTable>("NsiFreeTable");

    struct Table {
        void *keys;
        void *read_write;
        void *dynamic;
        FreeTable free;
    };
    constexpr auto free_table = [](Table *const table) noexcept {
        table->free(table->keys, table->read_write, table->dynamic, nullptr);
    };
    // `table` must be freed before `module` is unloaded, because its deleter
    // calls `NsiFreeTable` through a pointer into that DLL.
    auto table = wil::unique_struct<Table, decltype(free_table), free_table>{
        Table{.keys = nullptr, .read_write = nullptr, .dynamic = nullptr,
            .free = free}};

    // The following query matches `netsh` in x64 Windows build 10240 and
    // x64/ARM64 build 26100. `netsh` maps `store=active` to the first argument
    // value 1; table 2 in the IPv6 module has DWORD compartment-ID keys and
    // two 16-byte metadata records per key. Only the keys are used, but
    // requesting both metadata buffers preserves the query made by the
    // Windows implementation.
    constexpr auto ipv6_module = NPI_MODULEID{
        .Length = sizeof(NPI_MODULEID), .Type = MIT_GUID,
        .Guid = {.Data1 = 0xeb004a01, .Data2 = 0x9b1a, .Data3 = 0x11d4,
            .Data4 = {0x91, 0x23, 0x00, 0x50, 0x04, 0x77, 0x59, 0xbc}}};
    auto count = DWORD{};
    const auto result = allocate(1, &ipv6_module, 2,
        &table.keys, sizeof(NET_IF_COMPARTMENT_ID), &table.read_write, 16,
        &table.dynamic, 16, nullptr, 0, &count, 0);
    if (result != NO_ERROR) {
        throw std::system_error(result, std::system_category(),
            "could not enumerate network compartments");
    }
    return std::span{static_cast<const NET_IF_COMPARTMENT_ID *>(table.keys),
        count} | std::ranges::to<std::vector>();
}

} // namespace github_disable_ipv6
