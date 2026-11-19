CC       ?= gcc
CFLAGS   ?= -std=gnu11 -Wall -Wextra -Wpedantic -O2 -g
CPPFLAGS ?= -Iinclude -D_GNU_SOURCE
LDFLAGS  ?=

TARGET   := myshell
SRCDIR   := src
BUILDDIR := build
SOURCES  := $(wildcard $(SRCDIR)/*.c)
OBJECTS  := $(SOURCES:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)
DEPS     := include/myshell.h
.PHONY: all clean test install uninstall

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $@ $(LDFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c $(DEPS) | $(BUILDDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

test: $(TARGET)
	./tests/run_tests.sh
	./tests/test_file_input.sh
	./tests/test_ext.sh
	./tests/test_trace.sh
	./tests/test_dbg.sh
	./tests/test_rec.sh
	./tests/test_inspect.sh
	python3 tests/test_signals.py

install: $(TARGET)
	install -d $(DESTDIR)/usr/local/bin
	install -m 0755 $(TARGET) $(DESTDIR)/usr/local/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)/usr/local/bin/$(TARGET)

clean:
	rm -rf $(BUILDDIR) $(TARGET)
