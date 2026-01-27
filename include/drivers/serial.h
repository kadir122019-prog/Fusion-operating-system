#ifndef SERIAL_H
#define SERIAL_H

#include "types.h"

void serial_init(void);
void serial_write_char(char c);
void serial_write_str(const char *s);

#endif
