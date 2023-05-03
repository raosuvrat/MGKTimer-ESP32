#include "md_max.h"

static MD_Parola *md_max = NULL;
static bool number_font_wide = false;
static MD_MAX72XX::fontType_t narrow_numbers[] PROGMEM = {
    3, 255, 129, 255,  // 48 - '0'
    3, 4,   2,   255,  // 49 - '1'
    3, 249, 137, 143,  // 50 - '2'
    3, 137, 137, 255,  // 51 - '3'
    3, 15,  8,   255,  // 52 - '4'
    3, 143, 137, 249,  // 53 - '5'
    3, 255, 137, 249,  // 54 - '6'
    3, 7,   1,   255,  // 55 - '7'
    3, 255, 137, 255,  // 56 - '8'
    3, 15,  9,   255,  // 57 - '9'
    1, 102             // 58 - ':'
};
static MD_MAX72XX::fontType_t wide_numbers[] PROGMEM = {
    5, 255, 129, 129, 129, 255,  // 48 - '0'
    5, 0,   8,   4,   2,   255,  // 49 - '1'
    5, 249, 137, 137, 137, 143,  // 50 - '2'
    5, 137, 137, 137, 137, 255,  // 51 - '3'
    5, 15,  8,   8,   8,   255,  // 52 - '4'
    5, 143, 137, 137, 137, 249,  // 53 - '5'
    5, 255, 137, 137, 137, 249,  // 54 - '6'
    5, 7,   1,   1,   1,   255,  // 55 - '7'
    5, 255, 137, 137, 137, 255,  // 56 - '8'
    5, 15,  9,   9,   9,   255,  // 57 - '9'
    1, 102                       // 58 - ':'
};

void init_display(MD_Parola *md) {
  md->begin();
  md->displayClear();
  md->displaySuspend(false);
  md->setTextAlignment(PA_CENTER);
  md->setIntensity(15);
  md->setInvert(false);
  md->setCharSpacing(1);
  md_max = md;
  LOGF("Display initialized\n");
}

void display_print(const char *message) {
  // LOGF("PRINT: %s\n", message);
  md_max->print(message);
}

void display_time(unsigned long start, unsigned long end) {
  static char buf[64];
  unsigned long dur = end - start;
  int centisecs = dur / 10000;
  int secs = centisecs / 100;
  int mins = secs / 60;

  if (mins > 0) {
    if (number_font_wide) {
      for (int i = 0, j = 0; i < 11; ++i, j += narrow_numbers[j] + 1) {
        md_max->addChar('0' + i, narrow_numbers + j);
      }
      number_font_wide = false;
    }
    sprintf(buf, "%02d:%02d:%02d", mins % 60, secs % 60, centisecs % 100);
  } else {
    if (!number_font_wide) {
      for (int i = 0, j = 0; i < 11; ++i, j += wide_numbers[j] + 1) {
        md_max->addChar('0' + i, wide_numbers + j);
      }
      number_font_wide = true;
    }
    sprintf(buf, "%02d:%02d", secs % 60, centisecs % 100);
  }

  display_print(buf);
}

void display_intensity(int intensity) {
  if (intensity >= 0 && intensity <= 16) {
    md_max->setIntensity(intensity);
  }
}