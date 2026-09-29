CC      ?= cc
# Every buffer in the interface is fixed-size by design and relies on snprintf
# truncating rather than overflowing, so the truncation advisory is off.
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation
CPPFLAGS = -D_GNU_SOURCE -Isrc
LDFLAGS ?=
LDLIBS  = -lutil

BIN     = bin/ansimgr
SRCS    = $(shell find src -name '*.c' | sort)
OBJS    = $(SRCS:.c=.o)
DEPS    = $(OBJS:.o=.d)

.PHONY: all run clean install uninstall

all: $(BIN)

$(BIN): $(OBJS) | bin
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

bin:
	@mkdir -p bin

src/%.o: src/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -MP -c -o $@ $<

run: $(BIN)
	@./$(BIN)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/ansimgr

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/ansimgr

clean:
	rm -rf bin
	@find src -name '*.o' -delete
	@find src -name '*.d' -delete

-include $(DEPS)
