# Makefile with screen clearing and modular structure
SHELL = bash
DATE = $(shell printf '%(%Y%m%d)T')
CC = g++
CC_OPTS = -Wall -Wextra -O2 -std=c++23 -pthread -flto=4 -march=x86-64 -mtune=intel
CC_LIBS = -lodbc -lcurl -lcrypto -luuid -ljson-c -loath
CC_OBJS = env.o logger.o json_parser.o jwt.o httputils.o async.o email.o pkeyutil.o odbcutil.o http_client.o sql.o login.o server.o util.o main.o

.PHONY: all clean clear_screen

all: clear_screen apiserver

clear_screen:
	@clear

apiserver: $(CC_OBJS)
	$(CC) $(CC_OPTS) $(CC_OBJS) $(CC_LIBS) -o apiserver

main.o: src/main.cpp
	$(CC) $(CC_OPTS) -c src/main.cpp

server.o: src/server.cpp src/server.h
	$(CC) $(CC_OPTS) -DCPP_BUILD_DATE=$(DATE) -c src/server.cpp

login.o: src/login.cpp src/login.h
	$(CC) $(CC_OPTS) -c src/login.cpp

sql.o: src/sql.cpp src/sql.h
	$(CC) $(CC_OPTS) -c src/sql.cpp

http_client.o: src/http_client.cpp src/http_client.h
	$(CC) $(CC_OPTS) -c src/http_client.cpp

odbcutil.o: src/odbcutil.cpp src/odbcutil.h
	$(CC) $(CC_OPTS) -c src/odbcutil.cpp

email.o: src/email.cpp src/email.h
	$(CC) $(CC_OPTS) -c src/email.cpp

async.o: src/async.cpp src/async.hpp
	$(CC) $(CC_OPTS) -c src/async.cpp

httputils.o: src/httputils.cpp src/httputils.h
	$(CC) $(CC_OPTS) -c src/httputils.cpp

jwt.o: src/jwt.cpp src/jwt.h
	$(CC) $(CC_OPTS) -c src/jwt.cpp

json_parser.o: src/json_parser.cpp src/json_parser.h
	$(CC) $(CC_OPTS) -c src/json_parser.cpp

util.o: src/util.cpp src/util.h
	$(CC) $(CC_OPTS) -c src/util.cpp

pkeyutil.o: src/pkeyutil.cpp src/pkeyutil.h
	$(CC) $(CC_OPTS) -c src/pkeyutil.cpp

logger.o: src/logger.cpp src/logger.h
	$(CC) $(CC_OPTS) -c src/logger.cpp

env.o: src/env.cpp src/env.h
	$(CC) $(CC_OPTS) -c src/env.cpp

clean:
	rm -f $(CC_OBJS) apiserver
