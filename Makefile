# SPDX-FileCopyrightText: Copyright 2026 Cathy J. Fitzpatrick <cathy@cathyjf.com>
# SPDX-License-Identifier: GPL-3.0-or-later

SHELL := pwsh.exe
.SHELLFLAGS := -NoProfile -Command

.PHONY: all x64 arm64 clean

all: x64 arm64

x64: platform = x64
arm64: platform = ARM64

x64 arm64:
	cmake -S . -B build/$@ -G 'Visual Studio 18 2026' -A $(platform)
	cmake --build build/$@ --config Release --parallel
	cmake --install build/$@ --config Release --prefix dist

clean:
	cmake -E rm -rf build
