CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -g -Iinclude
LIBS ?= -lpthread -lm -lhackrf

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
BIN_DIR = bin

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))
TARGET = $(BIN_DIR)/fm-analyzer

.PHONY: all clean run help

all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) -o $@ $(LIBS)
	@echo "Built $@"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BUILD_DIR)/* $(BIN_DIR)/*
	@echo "Clean complete."

run: $(TARGET)
	./$(TARGET) | ffplay -nodisp -autoexit -probesize 32 -f s16le -ar 48000 -
