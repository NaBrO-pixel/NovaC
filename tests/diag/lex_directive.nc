#include <stdio.h>       // ERROR E1010
#define MAX 10           // ERROR E1010
  #ifdef DEBUG           // ERROR E1010
#endif                   // ERROR E1010
#pragma once             // ERROR E1010
# if 0                   // ERROR E1010
i32 f() { return 0; }
