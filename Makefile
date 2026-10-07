# SPDX-FileCopyrightText: Copyright 2026 Cathy J. Fitzpatrick <cathy@cathyjf.com>
# SPDX-License-Identifier: GPL-3.0-or-later

SHELL := pwsh.exe
.SHELLFLAGS := -NoProfile -Command

architectures := x64 arm64
install_targets := $(addprefix install-,$(architectures))

.PHONY: all $(architectures) install $(install_targets) clean

all: $(architectures)

$(architectures):
	cmake -S . -B build/$@ -G 'Visual Studio 18 2026' -A $@
	cmake --build build/$@ --config Release --parallel

install: $(install_targets)

$(install_targets): install-%: all
	cmake --install build/$* --config Release --prefix dist

clean:
	cmake -E rm -rf build
