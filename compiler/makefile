# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: AGPL-3.0-only

# make          build the compiler into build/one
# make test     build it and run its tests
# make check    build it and check every project in the repository
# make clean    remove the build

build := build

.PHONY: all test check clean

all:
	cmake -S . -B $(build)
	cmake --build $(build) --parallel

test: all
	ctest --test-dir $(build) --output-on-failure

check: all
	./$(build)/one check ../examples/library ../examples/tasks ../examples/tracker ../site

clean:
	rm -rf $(build)
