CFLAGS = -Wall -Werror -g
CC = gcc

default:
	$(CC) $(CFLAGS) smash.c -o smash