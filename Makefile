# ReelVault - Film Collection Browser
# Makefile for building the application

CC = gcc
PKGS = gtk+-3.0 sqlite3 libcurl json-c
CFLAGS = -Wall -Wextra -g -O2 $(shell pkg-config --cflags $(PKGS))
LDFLAGS = $(shell pkg-config --libs $(PKGS)) -lpthread

SRC_DIR = src
BUILD_DIR = build
TARGET = reelvault

SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean install uninstall test

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)/usr/bin/$(TARGET)
	install -Dm644 data/reelvault.desktop $(DESTDIR)/usr/share/applications/reelvault.desktop
	install -Dm644 data/icons/reelvault.svg $(DESTDIR)/usr/share/icons/hicolor/scalable/apps/reelvault.svg

uninstall:
	rm -f $(DESTDIR)/usr/bin/$(TARGET)
	rm -f $(DESTDIR)/usr/share/applications/reelvault.desktop
	rm -f $(DESTDIR)/usr/share/icons/hicolor/scalable/apps/reelvault.svg

test:
	@echo "Tests not yet implemented"

# Header dependencies
$(BUILD_DIR)/main.o: $(SRC_DIR)/app.h
$(BUILD_DIR)/window.o: $(SRC_DIR)/app.h $(SRC_DIR)/window.h $(SRC_DIR)/grid.h $(SRC_DIR)/filter.h
$(BUILD_DIR)/grid.o: $(SRC_DIR)/app.h $(SRC_DIR)/grid.h $(SRC_DIR)/db.h
$(BUILD_DIR)/detail.o: $(SRC_DIR)/app.h $(SRC_DIR)/detail.h $(SRC_DIR)/player.h
$(BUILD_DIR)/match.o: $(SRC_DIR)/app.h $(SRC_DIR)/match.h $(SRC_DIR)/scraper.h
$(BUILD_DIR)/filter.o: $(SRC_DIR)/app.h $(SRC_DIR)/filter.h $(SRC_DIR)/db.h
$(BUILD_DIR)/db.o: $(SRC_DIR)/db.h
$(BUILD_DIR)/scanner.o: $(SRC_DIR)/scanner.h $(SRC_DIR)/db.h $(SRC_DIR)/utils.h
$(BUILD_DIR)/scraper.o: $(SRC_DIR)/scraper.h $(SRC_DIR)/db.h $(SRC_DIR)/config.h
$(BUILD_DIR)/config.o: $(SRC_DIR)/config.h
$(BUILD_DIR)/player.o: $(SRC_DIR)/player.h $(SRC_DIR)/config.h
$(BUILD_DIR)/utils.o: $(SRC_DIR)/utils.h
