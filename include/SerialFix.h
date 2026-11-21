#pragma once
// Only for C++ translation units. Do not pull C++ headers into .c files.
#ifdef __cplusplus
  #include <HardwareSerial.h>
  #ifndef Serial
    #define Serial Serial0
  #endif
#endif
