#ifndef MD_MAX_H
#define MD_MAX_H

#include <MD_MAX72xx.h>
#include <MD_Parola.h>
#include <SPI.h>

#include "debug.h"
#include "pins.h"

#define MD_MAX_DEVICES 4

void init_display(MD_Parola *display);
void display_print(const char *);
void display_time(unsigned long start, unsigned long end);
void display_intensity(int intensity);

#endif