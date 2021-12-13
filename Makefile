# MIT License
#
# Copyright (c) 2021 Davidson Francis <davidsondfgl@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

#===================================================================
# Paths
#===================================================================

PREFIX ?= /usr/local
BINDIR  = $(PREFIX)/bin

#
# Enable -DDECODE_TO_FILE to enable file dump
#

#===================================================================
# Flags
#===================================================================

FFMPEG_LIBS = libavcodec \
	libavformat libswscale

CC ?= gcc
CFLAGS += -Wall -Wextra -std=c99 -pedantic -O3
CFLAGS += $(shell pkg-config --cflags $(FFMPEG_LIBS) sdl2)
LDLIBS  = $(shell pkg-config --libs --static $(FFMPEG_LIBS))
LDLIBS += $(shell pkg-config --libs sdl2)
LDLIBS += -lX11

TARGET = anipaper

C_SRC = anipaper.c util.c
OBJS = $(C_SRC:.c=.o)

.phony: all clean

# Build objects rule
%.o: %.c
	$(CC) $< $(CFLAGS) -c -o $@

all: $(TARGET)

# Anipaper
$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(CFLAGS) -o $@ $(LDFLAGS) $(LDLIBS)

# Install rules
install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)

# Uninstall rules
uninstall:
	$(RM) $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	$(RM) $(TARGET) $(OBJS)
