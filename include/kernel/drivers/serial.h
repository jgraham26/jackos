#pragma once

#define PORT 0x3f8 // COM1

int init_serial();
char read_serial();
void write_serial(char a);