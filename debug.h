#pragma once

#include <stdio.h>

#define C_RED "\033[1;31m"
#define C_RST "\033[0m"

#define FATAL(x...) do { \
  printf(C_RED "[-]" C_RST " FATAL ERROR : " x); \
  printf("\nLocation : %s(), %s:%u\n\n", \
         __FUNCTION__, __FILE__, __LINE__); \
  exit(1); \
  } while (0)

#define PFATAL(x...) do {\
  printf(C_RED "[-]" C_RST " FATAL ERROR : " x); \
  printf("\nLocation : %s(), %s:%u\n", \
         __FUNCTION__, __FILE__, __LINE__); \
  printf("System message: %s\n\n", strerror(errno)); \
  exit(1); \
  } while (0)

