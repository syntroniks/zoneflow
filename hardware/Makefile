# Builds every board in this directory. Today that is only the toolchain test.
BOARDS := toolchain-test

.PHONY: all $(BOARDS) design-check design-sync
all: $(BOARDS)
$(BOARDS):
	$(MAKE) -C $@

# The rev A paper design: netlist.yaml, pinmap.yaml, bom-preliminary.csv and
# the tables in docs/design/hardware-design.md. See tools/designsync/README.md.
# design-check changes nothing and fails on any disagreement. CI runs it.
# design-sync rewrites the generated tables and BOM columns from the netlist.
design-check:
	uv run ../tools/designsync/designsync.py check
design-sync:
	uv run ../tools/designsync/designsync.py write
