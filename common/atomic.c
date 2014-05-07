
#include "config.h"

#include "atomic.h"


extern inline RefCount IncrementRef(volatile RefCount *ptr);
extern inline RefCount DecrementRef(volatile RefCount *ptr);
extern inline int ExchangeInt(volatile int *ptr, int newval);
extern inline void *ExchangePtr(XchgPtr *ptr, void *newval);
extern inline int CompExchangeInt(volatile int *ptr, int oldval, int newval);
extern inline void *CompExchangePtr(XchgPtr *ptr, void *oldval, void *newval);
