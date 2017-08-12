CC=gcc
CFLAGS=-I.
DEPS = 
OBJ = tinysu.o 
OUT = tinysu

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

$(OUT): $(OBJ)
	gcc -o $@ $^ $(CFLAGS)

.PHONY: clean

clean:
	rm -f $(OUT) *.o
