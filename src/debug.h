#ifndef DEBUG_H
#define DEBUG_H

#ifndef DEBUG
#define DEBUG 0
#endif

#if DEBUG == 1
#define LOGF(...)               \
  do {                          \
    Serial.printf(__VA_ARGS__); \
  } while (0);
#else
#define LOGF(...)
#endif

#endif