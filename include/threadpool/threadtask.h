#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

typedef std::int64_t ThreadTaskPriority;

struct ThreadTask
{
    ThreadTaskPriority priority;
    std::function<void()> function;
};

typedef std::vector<ThreadTask> ThreadTaskVector;

static bool operator< (const ThreadTask& lhs, const ThreadTask& rhs)
{
    return lhs.priority < rhs.priority;
}
