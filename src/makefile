# Sources
TARGET = pgm
INCL = local.h
SRC = init.c mixdown.c play_record.c control.c util.c
OBJ = $(SRC:.c=.o)

# Compiler, Linker
CC = gcc
LIBS = -lasound -lm -lwiringPi -lrt
CFLAGS = -g `pkg-config --cflags --libs jack`

default: $(TARGET)

# create object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ): $(INCL)

# link into exe
$(TARGET): $(OBJ)
	$(CC) $(OBJ) $(CFLAGS) $(LIBS) -o $@

clean:
	rm -f *.o
	rm -f pgm



