All the code for the thread pooler is in one file for simplicity's sake. 

The main thread can run tasks completely independently from the thread pooler, and doesn't even need to get the results of the tasks if it doesn't want to.

The test uses 4 threads, if the thread num value is removed or set to 0 it will spawn as many thread as the systems hardware concurrency minus 1. 

