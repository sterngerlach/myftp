
# 情報工学科3年 学籍番号61610117 杉浦 圭祐
# Makefile

CC = gcc
CFLAGS = -Os -Wall -Wextra
LDFLAGS = 

OBJDIR = ./obj
OUTDIR = .

TARGET_SERVER = $(OUTDIR)/myftps
OBJECTS_SERVER = $(addprefix $(OBJDIR)/, \
	myftps.o myftps_context.o util.o myftp.o dynamic_string.o)
TARGET_CLIENT = $(OUTDIR)/myftpc
OBJECTS_CLIENT = $(addprefix $(OBJDIR)/, \
	myftpc.o myftpc_context.o util.o myftp.o dynamic_string.o)

default: server client

server: $(TARGET_SERVER)
client: $(TARGET_CLIENT)

debugs: CFLAGS += -ggdb
debugs: $(TARGET_SERVER)
debugc: CFLAGS += -ggdb
debugc: $(TARGET_CLIENT)

$(TARGET_SERVER): $(OBJECTS_SERVER)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^
$(TARGET_CLIENT): $(OBJECTS_CLIENT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(OBJDIR)/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	rm -f $(OBJECTS_SERVER)
	rm -f $(OBJECTS_CLIENT)

.PHONY: clean

