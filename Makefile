CC = ia16-elf-gcc
CFLAGS = -Os -mregparmcall -march=i8088 -Wall

DEPS = util.h

all: bricks.com

%.com: %.c $(DEPS)
	$(CC) $(CFLAGS) -o $@ $<

# Map file (to see where all the space is going)
%.map: %.c
	$(CC) $(CFLAGS) -o /tmp/a.com $<
	sed -i -E 's/(\s|^)[\/a-z0-9._\-]+\/ia16-elf\//\1:/g' $@.map

# Assembly source (not pretty) and diff from previous build
%.s: %.c $(DEPS)
	$(CC) $(CFLAGS) -S -fverbose-asm -o $@ $<
	sed -i -E 's/[0-9]*(_|tmp|# )[0-9]+/\1NNN/g;s/-?[0-9]+(\(%bp\))/_N\1/g' $@
	diff -U2 --color $@.old $@ || cp $@ $@.old

clean:
	$(RM) *.com *.lst *.map *.s
