int AggCheckCallContext(void* fcinfo, void* aggcontext) { return 1; }

void* MemoryContextSwitchTo(void* arg) { return arg; }

int pg_database_encoding_max_length(void) { return 1; }

void check_stack_depth(void) {}
