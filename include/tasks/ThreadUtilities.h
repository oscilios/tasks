#ifndef TASKS_REALTIME_H
#define TASKS_REALTIME_H

#include <thread>
#include <vector>

namespace tasks
{
    void setRealtimePriority(std::thread* thread);
    void flushDenormalsToZero();
    class ThreadJoiner;
}

#endif // TASKS_REALTIME_H
