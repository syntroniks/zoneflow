# Builds every board in this directory. Today that is only the toolchain test.
BOARDS := toolchain-test

.PHONY: all $(BOARDS)
all: $(BOARDS)
$(BOARDS):
	$(MAKE) -C $@
