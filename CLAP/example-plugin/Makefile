.PHONY: all build clean

all: build


Hello.clap:
	@g++ -shared -g -Wall -Wextra --std=gnu++17 -I../clap-headers/include \
		-Wno-unused-parameter -o Hello.clap plugin.cpp
	@rm -rf *.dSYM

build: Hello.clap


clean:
	@rm -rf Hello.clap *.dSYM