#include "ThreadPool.h"
#include <vld.h>

Job job1()
{
    std::cout << "job1" << std::endl;
    co_return;
}

Job job2()
{
    std::cout << "job2" << std::endl;
    co_return;
}

Job job3()
{
    std::cout << "job3" << std::endl;
    co_return;
}

Job anotherJob()
{
    std::cout << "anotherJob" << std::endl;
    co_return;
}

Job somethingDifferent()
{
    std::cout << "somethingDifferent" << std::endl;
    co_return;
}

Job anotherThing()
{
    std::cout << "anotherThing" << std::endl;
    co_return;
}

Job baseFunction()
{
    Job::all(job1(), job2(), job3())
        .then(anotherJob())
        .then(Job::all(somethingDifferent(), anotherThing()));
    co_return;
}

int main()
{
    for(uint64_t i = 0; i < 100000000; ++i)
    {
        baseFunction();
    }
    getGlobalThreadPool().waitIdle();
}