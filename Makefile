CC=gcc
PREFIX=/usr/local
SCDOC=/usr/bin/scdoc

SRC=$(wildcard src/*.c)
OBJ=$(addprefix obj/, $(notdir $(SRC:.c=.o)))

CFLAGS=-Wall -std=gnu17
LFLAGS=-L./src/lib/ftdi/build -lftd2xx -lreadline -lm -lbsd

all: uviemon uviemon.1

uviemon: $(OBJ)
	@echo "Link $@"
	$(CC) $(OBJ) $(LFLAGS) $(CFLAGS) -o uviemon

obj/%.o: src/%.c | obj
	@echo "CC $<"
	$(CC) $< -c $(CFLAGS) -o $@

obj:
	mkdir obj


uviemon.1: src/uviemon.1.scd $(SCDOC)
	$(SCDOC) < $< > $@


clean:
	rm -f $(OBJ) unviemon uviemon.1


install: all
	mkdir -p $(PREFIX)/bin $(PREFIX)/share/man/man1
	install -m755 uviemon $(PREFIX)/bin
	install -m644 uviemon.1 $(PREFIX)/share/man/man1

uninstall:
	rm -f $(PREFIX)/bin/uviemon
	rm -f $(PREFIX)/share/man/man1/uviemon.1

$(V).SILENT:
