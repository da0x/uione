# md — terminal Markdown renderer, a thin tool over the libmarkdown header
#
# Common targets:
#   make            build the ./md binary
#   make test       render README.md and the feature showcase, then run the
#                   layout test
#   make install    copy md to $(PREFIX)/bin and markdown.hpp to
#                   $(PREFIX)/include       (default /usr/local, needs sudo)
#   make uninstall  remove the installed md and markdown.hpp
#   make clean      remove build artifacts

CXX        ?= g++
CXXFLAGS   ?= -std=c++23 -O2 -Wall -Wextra -Wpedantic
PREFIX     ?= /usr/local
BINDIR     := $(PREFIX)/bin
INCLUDEDIR := $(PREFIX)/include

BIN        := md
HEADER     := include/markdown.hpp
OBJDIR     := obj
TEST_BIN   := $(OBJDIR)/layout_test

.PHONY: all test install uninstall clean

all: $(BIN)

# The library is one header, so md is one translation unit.
$(BIN): src/main.cpp $(HEADER)
	$(CXX) $(CXXFLAGS) -Iinclude $< -o $@

$(TEST_BIN): test/layout_test.cpp $(HEADER) | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -Iinclude $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

# Render bundled Markdown so you can eyeball the output, then check that the
# layout fits its width and carries no escape codes.
test: $(BIN) $(TEST_BIN)
	./$(BIN) README.md
	./$(BIN) test/sample.md
	./$(TEST_BIN) test/sample.md

install: $(BIN)
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(INCLUDEDIR)
	install -m 755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	install -m 644 $(HEADER) $(DESTDIR)$(INCLUDEDIR)/markdown.hpp
	@echo "installed $(BIN) -> $(DESTDIR)$(BINDIR)/$(BIN)"
	@echo "installed markdown.hpp -> $(DESTDIR)$(INCLUDEDIR)/markdown.hpp"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN) $(DESTDIR)$(INCLUDEDIR)/markdown.hpp
	@echo "removed $(DESTDIR)$(BINDIR)/$(BIN) and $(DESTDIR)$(INCLUDEDIR)/markdown.hpp"

clean:
	rm -rf $(OBJDIR) $(BIN)
