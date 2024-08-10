#ifndef THREADUTIL_H_
#define THREADUTIL_H_

#include <thread>
#include <functional>

namespace thread
{
	void launch_async(const std::function<void()> fn); 
}

#endif //THREADUTIL_H_