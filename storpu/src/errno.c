static __thread int errno;

int* __errno_location(void) { return &errno; }
