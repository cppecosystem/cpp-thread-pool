#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "threadtask.h"

typedef std::chrono::steady_clock SteadyClock;

class ThreadPool
{
protected:
    using FunctionType = std::function<void()>;
    using FunctionTypeVector = std::vector<FunctionType>;

    std::uint32_t batchSize = 1;

    std::priority_queue<ThreadTask, ThreadTaskVector, std::less<ThreadTask>> timedTaskQueue;
    std::mutex timedTaskQueueMutex;

    std::thread timedTaskThread;

    std::priority_queue<ThreadTask, ThreadTaskVector> workerTaskQueue;
    std::mutex workerTaskQueueMutex;

    std::vector<std::thread> workerThreadList;
    std::mutex workerThreadListMutex;

    std::uint32_t workerThreadCount;

    std::atomic<bool> paused;
    std::atomic<bool> running;

    void createThreads()
    {
        if (this->getThreadCount() > this->workerThreadList.size()) {
            for (std::vector<std::thread>::size_type i = this->workerThreadList.size(); i < this->getThreadCount(); i++) {
                {
                    std::scoped_lock lock(this->workerThreadListMutex);

                    this->workerThreadList.push_back(std::thread(&ThreadPool::worker, this));
                }
            }
        }
        else if (this->getThreadCount() < this->workerThreadList.size()) {
            this->destroyThreads();
        }
    }

    void destroyThreads()
    {
        for (std::vector<std::thread>::size_type i = this->workerThreadList.size(); i > this->getThreadCount(); i--) {
            std::scoped_lock lock(this->workerThreadListMutex);

            std::thread& oldThread = this->workerThreadList.back();

            if (oldThread.joinable()) {
                oldThread.join();
            }

            this->workerThreadList.pop_back();
        }
    }

    bool getNextTask(FunctionType& task)
    {
        std::scoped_lock lock(this->workerTaskQueueMutex);

        if (!this->hasTasks()) {
            return false;
        }

        ThreadTask threadTask = this->workerTaskQueue.top();
        this->workerTaskQueue.pop();

        task = threadTask.function;

        return true;
    }

    bool getNextTimedTask(FunctionType& task)
    {
        std::scoped_lock lock(this->timedTaskQueueMutex);

        if (!this->hasTimedTasks()) {
            return false;
        }

        ThreadTask threadTask = this->timedTaskQueue.top();

        std::time_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                SteadyClock::now().time_since_epoch()).count();

        if (threadTask.priority > now) {
            return false;
        }

        this->timedTaskQueue.pop();

        task = threadTask.function;

        return true;
    }

    bool getNextTaskBatch(ThreadTaskVector& taskVector)
    {
        std::scoped_lock lock(this->workerTaskQueueMutex);

        for (std::uint32_t i = 0; i < this->batchSize; i++) {
            if (!this->hasTasks()) {
                return i > 0;
            }

            ThreadTask threadTask = this->workerTaskQueue.top();
            this->workerTaskQueue.pop();

            taskVector.push_back(threadTask);
        }

        return true;
    }

    void timedWorker()
    {
        FunctionType task;

        do {
            while (this->getNextTimedTask(task)) {
                this->addTask(ULLONG_MAX, task);
            }

            //std::this_thread::yield();
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        } while (this->isRunning());
    }

    void worker()
    {
        ThreadTaskVector taskVector;

        do {
            while (!isPaused()
                   && this->getNextTaskBatch(taskVector)) {
                for (ThreadTaskVector::iterator it = taskVector.begin(); it != taskVector.end(); ++it) {
                    ThreadTask& task = (*it);
                    std::invoke(task.function);
                }

                taskVector.clear();
            }

            //std::this_thread::yield();
            std::this_thread::sleep_for(std::chrono::microseconds(1000));
        } while (this->isRunning());
    }
public:
    ThreadPool()
    {
        this->paused = false;
        this->running = true;

        this->setThreadCount(std::thread::hardware_concurrency());

        this->timedTaskThread = std::thread(&ThreadPool::timedWorker, this);
    }

    ~ThreadPool()
    {
        this->stop();
    }

    template <typename T, typename... A>
    void addTask(ThreadTaskPriority priority, const T& task, const A&...args)
    {
        ThreadTask threadTask{ priority,[task, args...] {
            task(args...);
        } };

        std::scoped_lock lock(this->workerTaskQueueMutex);

        this->workerTaskQueue.push(threadTask);
    }

    template <typename T, typename... A>
    void addTimedTask(std::uint32_t inMilliseconds, const T& task, const A&...args)
    {
        std::time_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                SteadyClock::now().time_since_epoch()).count();

        ThreadTask threadTask{ static_cast<ThreadTaskPriority>(now + inMilliseconds),[task, args...] {
            task(args...);
        } };

        std::scoped_lock lock(this->timedTaskQueueMutex);

        this->timedTaskQueue.push(threadTask);
    }

    std::uint32_t getThreadCount() const
    {
        return this->workerThreadCount;
    }

    bool hasTasks()
    {
        return !this->workerTaskQueue.empty();
    }

    bool hasTimedTasks()
    {
        return !this->timedTaskQueue.empty();
    }

    bool isPaused() const
    {
        return this->paused;
    }

    bool isRunning() const
    {
        return this->running;
    }

    void pause()
    {
        this->paused = true;
    }

    void setBatchSize(std::uint32_t batchSize)
    {
        this->batchSize = batchSize;
    }

    void setThreadCount(std::uint32_t threadCount)
    {
        this->workerThreadCount = threadCount;

        this->createThreads();
    }

    void stop()
    {
        this->waitForEmptyQueue();

        this->running = false;

        this->setThreadCount(0);

        this->timedTaskThread.join();
    }

    void unpause()
    {
        this->paused = false;
    }

    void waitForEmptyQueue()
    {
        while (this->hasTasks()) {
            std::this_thread::yield();
        }
    }
};
