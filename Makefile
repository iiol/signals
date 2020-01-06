TARGET = a.out

CFLAGS = -pedantic -I include -ggdb -Wall -Wextra -Wno-pedantic -std=c99 -fplan9-extensions #-O2
LFLAGS = -lm -lallegro -lallegro_main -lallegro_image -lallegro_font \
	-lallegro_ttf -lallegro_primitives -lm

SRC = src/main.c src/wind.c src/style.c
OBJ = $(SRC:.c=.o)

.PHONY: clean

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LFLAGS) -o $(TARGET) $<

clean:
	rm $(TARGET)
