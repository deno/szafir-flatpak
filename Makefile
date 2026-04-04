SHELL := /bin/sh

PROXY_VERSION := $(shell python3 scripts/query_releases.py --current-version)
SOURCE_BUILD_DIR := buildsrc
SOURCE_ARCHIVE := $(SOURCE_BUILD_DIR)/szafir-host-proxy-$(PROXY_VERSION)-source.tar.gz

.PHONY: dist
dist:
	cmake -S szafir-host-proxy -B $(SOURCE_BUILD_DIR) \
		-DSZAFIR_HOST_PROXY_SOURCE_PACKAGE_ONLY=ON \
		-DAPP_VERSION=$(PROXY_VERSION)
	cmake --build $(SOURCE_BUILD_DIR) --target package_source
	@echo "Built $(SOURCE_ARCHIVE)"
