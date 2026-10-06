// SPDX-FileCopyrightText: Copyright 2026 Cathy J. Fitzpatrick <cathy@cathyjf.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netcfgx.h>
#include <memory>
#include <wil/com.h>
#include <wil/resource.h>
#include <wil/safecast.h>

import std;
import <cstdio>;
import github_disable_ipv6.nsi;

namespace {

// Uninitializing network configuration or releasing its write lock can fail
// during exception unwinding. A diagnostic that throws from `session`'s
// nonthrowing deleter would terminate the process and prevent the original
// exception from reaching its handler, so diagnostic failures are discarded.
auto ReportCleanupFailure(const HRESULT result,
    const char *const operation) noexcept -> void {
    if (SUCCEEDED(result)) {
        return;
    }
    try {
        std::println(stderr, "{}: HRESULT 0x{:08x}", operation,
            std::bit_cast<unsigned long>(result));
    } catch (...) {}
    std::fflush(stderr);
}

auto DisableIpv6Bindings() -> bool {
    const auto apartment = wil::CoInitializeEx();
    const auto configuration = wil::CoCreateInstance<INetCfg>(CLSID_CNetCfg);
    const auto lock = configuration.query<INetCfgLock>();
    struct Session {
        INetCfg *initialized;
        INetCfgLock *locked;
    };
    constexpr auto end_session = [](Session *const session) noexcept {
        if (session->initialized) {
            ReportCleanupFailure(session->initialized->Uninitialize(),
                "Could not uninitialize network configuration");
        }
        if (session->locked) {
            ReportCleanupFailure(session->locked->ReleaseWriteLock(),
                "Could not release the network-configuration lock");
        }
    };
    // The network configuration must be uninitialized before releasing its
    // write lock; otherwise `ReleaseWriteLock` returns
    // `NETCFG_E_ALREADY_INITIALIZED`.
    //
    // Each non-null `Session` field records a successful acquisition. If
    // `Initialize` fails, cleanup releases the acquired lock without calling
    // `Uninitialize` for a failed initialization.
    //
    // `configuration` and `lock` are declared before `session` so their COM
    // objects remain alive until its cleanup calls have finished.
    const auto session = [&configuration, &lock] {
        auto session = wil::unique_struct<Session,
            decltype(end_session), end_session>{};
        auto lock_holder = wil::unique_cotaskmem_string{};
        // Another program may already hold the network-configuration write
        // lock. To avoid delaying CI preparation, `AcquireWriteLock` is given
        // a zero timeout: an occupied lock is reported instead of waiting for
        // its holder. `Run` still attempts address deletion after this failure.
        const auto result = lock->AcquireWriteLock(0,
            L"DeviceFs CI IPv6 removal", lock_holder.put());
        THROW_IF_FAILED_MSG(result, "could not lock network configuration");
        if (result == S_FALSE) {
            THROW_HR_MSG(HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION),
                "network configuration is locked by '%ls'",
                lock_holder.get());
        }
        session.locked = lock.get();
        THROW_IF_FAILED_MSG(configuration->Initialize(nullptr),
            "could not initialize network configuration");
        session.initialized = configuration.get();
        return session;
    }();

    auto component = wil::com_ptr<INetCfgComponent>{};
    THROW_IF_FAILED_MSG(configuration->FindComponent(
        L"ms_tcpip6", component.put()), "could not find 'ms_tcpip6'");
    THROW_HR_IF_MSG(HRESULT_FROM_WIN32(ERROR_NOT_FOUND), !component,
        "the IPv6 component 'ms_tcpip6' was not found");
    const auto bindings = component.query<INetCfgComponentBindings>();
    auto paths = wil::com_ptr<IEnumNetCfgBindingPath>{};
    // IPv6 can be bound to multiple network adapters. Enumerating paths that
    // start at the IPv6 component lets us disable its bindings without
    // selecting adapters individually; `EBP_BELOW` requests those paths.
    THROW_IF_FAILED_MSG(bindings->EnumBindingPaths(EBP_BELOW, paths.put()),
        "could not enumerate IPv6 binding paths");
    auto succeeded = true;
    while (true) {
        auto path = wil::com_ptr<INetCfgBindingPath>{};
        const auto next_result = paths->Next(1, path.put(), nullptr);
        if (next_result == S_FALSE) {
            break;
        }
        if (FAILED(next_result)) {
            std::println(stderr,
                "Could not enumerate IPv6 binding paths: {}",
                wil::ResultException{next_result}.what());
            std::fflush(stderr);
            succeeded = false;
            break;
        }

        const auto result = path->Enable(FALSE);
        if (SUCCEEDED(result)) {
            continue;
        }
        std::println(stderr,
            "Could not disable IPv6 binding path: {}",
            wil::ResultException{result}.what());
        std::fflush(stderr);
        succeeded = false;
    }
    // `Enable(FALSE)` records proposed binding changes; `Apply` applies them
    // to the operating system. If `Next` fails, earlier successful disable
    // requests should still be applied, rather than discarded with the failed
    // enumeration.
    const auto apply_result = configuration->Apply();
    THROW_IF_FAILED_MSG(apply_result,
        "could not apply IPv6 binding changes");
    if (apply_result == NETCFG_S_REBOOT) {
        std::println("IPv6 binding changes require a reboot.");
        std::fflush(stdout);
        return false;
    }
    return succeeded;
}

auto RemoveIpv6Addresses(const NET_IF_COMPARTMENT_ID compartment) -> bool {
    const auto select_result = SetCurrentThreadCompartmentId(compartment);
    if (select_result != NO_ERROR) {
        throw std::system_error(select_result, std::system_category(),
            std::format("could not select network compartment {}",
                compartment));
    }
    auto table = wil::unique_any<MIB_UNICASTIPADDRESS_TABLE *,
        decltype(&FreeMibTable), FreeMibTable>{};
    const auto result = GetUnicastIpAddressTable(AF_INET6, table.put());
    if (result == ERROR_NOT_FOUND) {
        return true;
    }
    if (result != NO_ERROR) {
        throw std::system_error(result, std::system_category(),
            std::format("could not query IPv6 addresses in compartment {}",
                compartment));
    }
    auto succeeded = true;
    for (const auto &row :
        std::span{table.get()->Table, table.get()->NumEntries}) {
        const auto deletion_result = DeleteUnicastIpAddressEntry(&row);
        if (deletion_result == NO_ERROR) {
            continue;
        }
        auto address = std::array<char, INET6_ADDRSTRLEN>{};
        const auto formatted = InetNtopA(AF_INET6,
            &row.Address.Ipv6.sin6_addr, address.data(), address.size());
        const auto error = std::system_error(deletion_result,
            std::system_category());
        std::println(stderr,
            "Could not delete IPv6 address '{}' on interface {} in "
            "compartment {}: {} (Win32 error {})",
            formatted ? formatted : "(unavailable)", row.InterfaceIndex,
            compartment, error.what(), deletion_result);
        std::fflush(stderr);
        succeeded = false;
    }
    return succeeded;
}

auto Run(const std::span<char *const> argv) -> int {
    const auto arguments = argv.subspan(1);
    if ((arguments.size() == 1) &&
        (std::string_view{arguments.front()} == "--help")) {
        std::println("Usage: {} [COMPARTMENT_ID ...]", argv[0]);
        std::println("Disable IPv6 bindings and delete IPv6 addresses. "
            "Without IDs, enumerate all compartments.");
        return 0;
    }
    auto requested_compartments = arguments |
        std::views::transform([](const char *const text) {
            const auto input = std::string_view{text};
            auto compartment = NET_IF_COMPARTMENT_ID{};
            const auto [end, error] = std::from_chars(input.data(),
                input.data() + input.size(), compartment);
            if ((error != std::errc{}) ||
                (end != input.data() + input.size())) {
                throw std::runtime_error(std::format(
                    "invalid network compartment ID '{}'", input));
            }
            return compartment;
        }) | std::ranges::to<std::vector>();
    auto succeeded = [] {
        try {
            return DisableIpv6Bindings();
        } catch (const std::exception &error) {
            std::println(stderr,
                "Could not disable IPv6 bindings: {}", error.what());
            std::fflush(stderr);
            return false;
        }
    }();
    const auto compartments = requested_compartments.empty()
        ? github_disable_ipv6::GetNetworkCompartments()
        : std::move(requested_compartments);
    for (const auto compartment : compartments) {
        try {
            if (RemoveIpv6Addresses(compartment)) {
                continue;
            }
        } catch (const std::exception &error) {
            std::println(stderr,
                "Could not remove IPv6 addresses in compartment {}: {}",
                compartment, error.what());
            std::fflush(stderr);
        }
        succeeded = false;
    }
    return succeeded ? 0 : 1;
}

} // namespace

[[gsl::suppress("26429", justification:
    "C++ [basic.start.main] requires `argv[argc]` to be null, which requires "
    "`argv` itself to point to a valid array.")]]
auto main(_Pre_satisfies_(argc > 0) const int argc,
    _In_reads_(argc) char **const argv) -> int {
    try {
        return Run(std::span{argv, argv + argc});
    } catch (const std::exception &error) {
        std::println(stderr, "{}: {}", argv[0], error.what());
        std::fflush(stderr);
        return 1;
    }
}
