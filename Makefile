# NovaC build.
#
# Phase 1: only documentation checks exist. The compiler targets (`all`,
# `test`) arrive with Phase 2; see docs/NOVAC_ROADMAP.md.
#
#   make check-docs     documentation consistency checks
#   make check          everything that can be checked at this phase

PYTHON ?= python3

.PHONY: check check-docs help

check: check-docs

check-docs:
	$(PYTHON) tools/check_docs.py

help:
	@echo "targets: check, check-docs"
	@echo "the compiler does not exist yet (Phase 2); see docs/NOVAC_ROADMAP.md"
