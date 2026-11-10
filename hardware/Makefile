# Builds every board in this directory: the toolchain test and the product
# board, revision A.
BOARDS := toolchain-test zoneflow-a

.PHONY: all $(BOARDS) design-check design-sync
all: $(BOARDS)
$(BOARDS):
	$(MAKE) -C $@

# The rev A paper design: netlist.yaml, pinmap.yaml, bom-preliminary.csv and
# the tables in docs/design/hardware-design.md. See tools/designsync/README.md.
# design-check changes nothing and fails on any disagreement. CI runs it. Its
# second step compares the netlist and BOMs that KiCad extracted from the
# rev A schematic, committed in zoneflow-a/output, with netlist.yaml.
# design-sync rewrites the generated tables and BOM columns from the netlist.
design-check:
	uv run ../tools/designsync/designsync.py check
	$(MAKE) -C zoneflow-a committed
design-sync:
	uv run ../tools/designsync/designsync.py write
