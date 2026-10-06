# github-disable-ipv6

Although IPv6 was adopted as an Internet Standard in 2017, GitHub runners
apparently do not provide outbound IPv6 connectivity.
See <https://github.com/actions/runner-images/issues/668#issuecomment-624080758>.

Despite the lack of IPv6 connectivity, some runners come configured with
network interfaces that include IPv6 addresses. This can cause software to
query AAAA records and attempt connection to unreachable IPv6 addresses.

This action deletes IPv6 addresses from network interfaces on a runner.
The action supports GNU/Linux, macOS, and Windows.

The Windows action uses native code because the PowerShell cmdlets for
disabling IPv6 are very slow. The native code in this repository avoids
loading WMI/CIM, so it should be considerably more performant than the
relevant PowerShell cmdlets.
