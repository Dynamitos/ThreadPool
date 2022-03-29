#include "ThreadPool.h"
#include <memory_resource>

std::mutex promisesLock;
std::list<Promise*> promises;

//Event::Event(nullptr_t)
//{
//}

Event::Event(const std::string &name, const std::source_location &location)
    : state(std::make_shared<EventState>())
{
    state->name = name;
    state->location = location;
}

Event::Event(const std::source_location &location)
    : state(std::make_shared<EventState>())
{
    state->name = location.function_name();
    state->location = location;
}

Event::Event(const Event& other)
{
    std::scoped_lock lock(other.eventLock);
    state = other.state;
}

Event::Event(Event&& other)
{
    std::scoped_lock lock(other.eventLock);
    state = std::move(other.state);
}

Event& Event::operator=(const Event& other)
{
    if(this != &other)
    {
        std::scoped_lock lock(eventLock, other.eventLock);
        state = other.state;
    }
    return *this;
}
Event& Event::operator=(Event&& other)
{
    if(this != &other)
    {
        std::scoped_lock lock(eventLock, other.eventLock);
        state = std::move(other.state);
    }
    return *this;
}

void Event::raise()
{
    std::scoped_lock lock(eventLock);
    state->data = true;
    if(state->waitingJobs.size() > 0)
    {
        getGlobalThreadPool().scheduleBatch(state->waitingJobs);
        state->waitingJobs.clear();
    }
    if(state->waitingMainJobs.size() > 0)
    {
        getGlobalThreadPool().scheduleBatch(state->waitingMainJobs);
        state->waitingMainJobs.clear();
    }
}
void Event::reset()
{
    std::scoped_lock lock(eventLock);
    state->data = false;
}

bool Event::await_ready()
{
    eventLock.lock();
    bool result = state->data;
    if(result)
    {
        eventLock.unlock();
    }
    return result;
}

void Event::await_suspend(std::coroutine_handle<JobPromiseBase<false>> h)
{
    state->waitingJobs.push_back(JobBase<false>(&h.promise()));
    eventLock.unlock();
}

void Event::await_suspend(std::coroutine_handle<JobPromiseBase<true>> h)
{
    state->waitingMainJobs.push_back(JobBase<true>(&h.promise()));
    eventLock.unlock();
}

ThreadPool::ThreadPool(uint32_t threadCount)
    : workers(threadCount)
{
    running.store(true);
    for (uint32_t i = 0; i < threadCount; ++i)
    {
        workers[i] = std::thread(&ThreadPool::threadLoop, this);
    }
}

ThreadPool::~ThreadPool()
{
    running.store(false);
    {
        std::unique_lock lock(mainJobLock);
        mainJobCV.notify_all();
    }
    {
        std::unique_lock lock(jobQueueLock);
        jobQueueCV.notify_all();
    }
    for(auto& worker : workers)
    {
        worker.join();
    }
}

void ThreadPool::waitIdle()
{
    while(true)
    {
        std::unique_lock lock(numIdlingLock);
        if(numIdling == workers.size())
        {
            assert(promises.size() == 0);
            return;
        }
        numIdlingIncr.wait(lock);
    }
}
void ThreadPool::scheduleJob(Job job)
{
    assert(!job.done());
    std::scoped_lock lock(jobQueueLock);
    jobQueue.push_back(std::move(job));
    jobQueueCV.notify_one();
}
void ThreadPool::scheduleJob(MainJob job)
{
    assert(!job.done());
    std::scoped_lock lock(mainJobLock);
    mainJobs.push_back(std::move(job));
    mainJobCV.notify_one();
}
void ThreadPool::mainLoop()
{
    while(running.load())
    {
        MainJob job;
        {
            std::unique_lock lock(mainJobLock);
            if(mainJobs.empty())
            {
                mainJobCV.wait(lock);
            }
            [[likely]]
            if(!mainJobs.empty())
            {
                job = mainJobs.front();
                mainJobs.pop_front();
            }
            else
            {
                continue;
            }
        }
        job.resume();
    }
}

void ThreadPool::threadLoop()
{
    std::list<Job> localQueue;
    while (running.load())
    {
        [[likely]]
        if(!localQueue.empty())
        {
            Job job = localQueue.front();
            localQueue.pop_front();
            job.resume();
        }
        else
        {
            std::unique_lock lock(jobQueueLock);
            if (jobQueue.empty())
            {
                {
                    std::unique_lock lock2(numIdlingLock);
                    numIdling++;
                    numIdlingIncr.notify_one();
                }
                jobQueueCV.wait(lock);
                {
                    std::unique_lock lock2(numIdlingLock);
                    numIdling--;
                }
            }
            // take 1/numThreads jobs, maybe make this a parameter that
            // adjusts based on past workload
            uint32_t partitionedWorkload = (uint32_t)(jobQueue.size() / workers.size());
            uint32_t numTaken = std::clamp(partitionedWorkload, 1u, localQueueSize);
            while (!jobQueue.empty() && localQueue.size() < numTaken)
            {
                localQueue.push_back(jobQueue.front());
                jobQueue.pop_front();
            }
        }
    }
}
ThreadPool& getGlobalThreadPool()
{
    static ThreadPool threadPool;
    return threadPool;
}