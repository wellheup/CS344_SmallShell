#!/bin/bash
#Phillip Wellheuser
#Compiles SmallShell program

echo Now compiling and running the SmallShell C program. Try entering basic console commands like "'ls'", "'cd'", or "'status'". 

gcc -g -std=gnu99 smallsh.c -o smallsh
./smallsh