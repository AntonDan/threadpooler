#pragma once

#include <iostream>
#include <thread>
#include <mutex>
#include <queue>
#include <future>

#define MAX(A, B) ((A > B) ? A : B)

class IThreadTask {
public:
    virtual const void Invoke() = 0;
};

template<typename T>
class PackagedThreadTask : IThreadTask {
private:
    std::packaged_task<T()>* task;

public:
    PackagedThreadTask(std::packaged_task<T()>* packaged_task) {
        task = packaged_task;
    }

    virtual const void Invoke() override
    {
        if (task != nullptr) {
            (*task)();
        }
        else
        {
            printf("Attempting to invoke destroyed task");
        }
    }

    ~PackagedThreadTask() {
        if (task != nullptr) {
            delete task;
        }
    }
};

class WorkloadQueue {
private:
    std::mutex taskWaitMtx;
    std::mutex taskQueueMtx;
    std::condition_variable flag;
    std::deque<IThreadTask*> tasks;
    std::atomic_bool shouldClear;
    uint32_t runningTasksNum;

public:
    WorkloadQueue() {
        printf("Creating Workload Data\n");
        shouldClear = false;
        {
            std::unique_lock<std::mutex> lock(taskQueueMtx); // Note: Not really needed to lock the mutex here but doing it anyway
            runningTasksNum = 0;
        }
    }

    IThreadTask* GetTask() {
        std::unique_lock<std::mutex> lock(taskWaitMtx);
        flag.wait(lock, [this]() { return !tasks.empty() || shouldClear; });

        if (shouldClear) return nullptr; // Note: I don't like how this is the way to make threads stop waiting for a task. 

        IThreadTask* task = nullptr;
        {
            std::unique_lock<std::mutex> lock(taskQueueMtx);
            task = tasks.front();
            tasks.pop_front();
            ++runningTasksNum;
            printf("Popped task. Running tasks %i \n", runningTasksNum);
        }
        return task;
    }

    template<typename T>
    std::future<T> QueueTask(const std::function<T()>& function) {
        std::packaged_task<T()>* packagedTask = new std::packaged_task<T()>(function);
        std::future<T> future = packagedTask->get_future();
        IThreadTask* task = (IThreadTask*)new PackagedThreadTask<T>(packagedTask);
        QueueTaskInternal(task);
        return future;
    }

    void MarkTaskAsDone(IThreadTask* task) { // Note: I created this method because I didn't like the workers deleting tasks. Perhaps this could include more functionality
        {
            std::unique_lock<std::mutex> lock(taskQueueMtx);
            if (task != nullptr) {
                delete task;
            }
            --runningTasksNum;
            printf("Marked task as done. Running tasks %i \n", runningTasksNum);
        }
    }

    void Clear() {
        printf("Clearing Workload Data\n");
        shouldClear = true;
        flag.notify_all();
    }

    ~WorkloadQueue()
    {
        {
            std::unique_lock<std::mutex> lock(taskQueueMtx);
            for (IThreadTask* task : tasks) {
                delete task;
            }
            tasks.clear();
        }
    }

private:
    void QueueTaskInternal(IThreadTask* const task) {
        if (shouldClear) {
            printf("Not allowed to queue task\n");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(taskQueueMtx);
            tasks.push_back(task);
            printf("Queued task\n");
        }
        flag.notify_one();
    }
};

class WorkerThread {
private:
    const uint32_t id;
    WorkloadQueue* workloadQueue;
    std::atomic_bool isBusy;
    std::atomic_bool shouldStop;
    std::thread thread;

public:
    explicit WorkerThread(const uint32_t id, WorkloadQueue* data) : id(id), workloadQueue(data) {
        shouldStop = false;
        isBusy = false;
        printf("Thread %u: Created\n", id);
    }

    void Start() {
        thread = std::thread(&WorkerThread::Run, this);
    }

    void Stop() {
        printf("Thead %u: Stopping\n", id);
        shouldStop = true;
        if (thread.joinable())
            thread.join();
        printf("Thead %u: Stopped\n", id);
    }

private:
    void Run() {
        printf("Thead %u: Running\n", id);
        while (!shouldStop) {
            IThreadTask* task = workloadQueue->GetTask();
            if (task == NULL) {
                break;
            }
            else {
                isBusy = true;
                printf("Thead %u: Began work\n", id);
                task->Invoke();
                workloadQueue->MarkTaskAsDone(task);
                isBusy = false;
                printf("Thead %u: Finished work\n", id);
            }
        }
        printf("Thead %u: Exiting\n", id);
    }
};

class TaskScheduler {
private:
    WorkloadQueue * workloadQueue;
    std::mutex threadQueueMtx;
    std::vector<WorkerThread*> threads;

public:
    explicit TaskScheduler(const uint32_t size = 0) {
        uint32_t threadNum = size; (size > 0) ? size : MAX(std::thread::hardware_concurrency() - 1, 1); // Making sure that there is at least 1 thread spawned

        workloadQueue = new WorkloadQueue();
        std::lock_guard<std::mutex> lock(threadQueueMtx);
        for (uint32_t i = 0; i < threadNum; ++i) {
            WorkerThread* workerThread = new WorkerThread(i, workloadQueue);
            workerThread->Start();
            threads.push_back(workerThread);
        }
    }

    template<typename _Fn, typename... _Args>
    std::future<std::result_of_t<_Fn(_Args...)>> ScheduleTask(_Fn&& _Fx, _Args&&... _Ax) {
        using return_type = std::result_of_t<_Fn(_Args...)>;

        auto function = std::bind(std::forward<_Fn>(_Fx), std::forward<_Args>(_Ax)...);
        return workloadQueue->QueueTask<return_type>(function);
    }

    ~TaskScheduler()
    {
        std::lock_guard<std::mutex> lock(threadQueueMtx);
        workloadQueue->Clear();
        for (WorkerThread* thread : threads) {
            thread->Stop();
        }

        printf("Freeing Allocated memory\n");
        delete workloadQueue;
        for (WorkerThread* thread : threads) {
            delete thread;
        }
        threads.clear();
    }
};
