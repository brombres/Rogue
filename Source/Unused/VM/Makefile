CC=gcc
CFLAGS = -Wall

C_FILES = $(shell find *.c | grep -v temp)
H_FILES   = $(shell find *.h | grep -v temp)

O_FILES = $(C_FILES:%.c=Build/%.o)

all: Build a.out
	./a.out

etc:
	roguec MakeETC.rogue --execute

Build:
	@mkdir -p Build

a.out: $(O_FILES)
	$(CC) $(CFLAGS) $(O_FILES) -o a.out

Build/%.o: %.c $(H_FILES)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf Build
