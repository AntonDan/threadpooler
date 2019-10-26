#include <iostream>
#include "threadpool.h"

uint64_t foo(const uint64_t x) {
    uint64_t sum = 0;
    for (int i = 0; i < x; ++i) {
        sum += i;
        if (i % 10 == 0) {
            sum /= 2;
        }
    }
    return sum;
}

int main(void) {
    TaskScheduler* ts = new TaskScheduler(4);

    std::vector<std::future<uint64_t>> futures;

    for (uint32_t i = 1; i <= 20; ++i) {
        uint64_t value = i * 1000;
        futures.push_back(ts->ScheduleTask(foo, value*value));
    }

    std::future<uint64_t> future1 = ts->ScheduleTask(foo, 1000000);
    std::future<uint64_t> future2 = ts->ScheduleTask(foo, 500000000);
    uint64_t result = future1.get();
    printf("Got result %llu\n", result);

    printf("Main thread doing some work\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    printf("Main thread done doing its work\n");

    for (uint32_t i = 0; i < futures.size(); ++i) {
        uint64_t result = futures[i].get();
        printf("Got result %llu\n", result);
    }

    result = future2.get();
    printf("Got result %llu\n", result);
    ts->ScheduleTask(foo, 500000000); // Note: Even though we do not get the result of this task, the threadpool doesn't destroy itself unless all queued tasks are completed. It shouldn't be hard to change that behavior however.

    delete ts;
    //printf("Test Completed\n");
    getchar();
    return 0;
}