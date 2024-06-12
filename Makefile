CC = gcc
CFLAGS += -Wall -Werror -Wextra -g
LDFLAGS ?=
#LDFLAGS = -lssl -lcrypto
OUTPUT ?= iceFUNprog
PREFIX ?= /usr/local
SRCS=*.c

$(OUTPUT): $(SRCS)
	$(CC) $(LDFLAGS) $(CFLAGS) -D_FILEHASH_MD5SUM=$(shell md5sum $(SRCS) | grep -o "^[0-9A-Fa-f]*[[:space:]]") -o $@ $^
	@#$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $^

# phony targets
.PHONY: all
all: $(OUTPUT)
	@echo Target $(TARGET) build finished.

.PHONY: install
install: $(OUTPUT)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(OUTPUT) $(DESTDIR)$(PREFIX)/bin/

.PHONY: uninstall
uninstall: $(OUTPUT)
	rm $(DESTDIR)$(PREFIX)/bin/$(OUTPUT)

.PHONY: clean
clean:
	rm -f *.o $(OUTPUT)
