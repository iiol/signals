TARGET = a.out

CFLAGS = -pedantic -I include #-O2
LFLAGS = -lm -lallegro -lallegro_main -lallegro_image -lallegro_font \
	-lallegro_ttf -lallegro_primitives -lm

SRC = main.c
OBJ = $(SRC:.c=.o)

$(TARGET): $(SRC) test.c
	$(CC) $(CFLAGS) $(LFLAGS) -o $(TARGET) $(SRC)
