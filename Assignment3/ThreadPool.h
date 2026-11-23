#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <algorithm>

// A pool of worker threads that are created once and reused for every task.
class ThreadPool {
public:
	// creates numThreads worker threads, each one running workerThread()
	// (a pool of 0 threads would accept tasks and never run them, so at least one worker is always created)
	explicit ThreadPool(size_t numThreads) : stop(false) {
		const size_t workers = std::max<size_t>(1, numThreads);
		for (size_t i = 0; i < workers; i++) {
			threads.emplace_back(&ThreadPool::workerThread, this);
		}
	}

	// lets the workers finish every task already in the queue, then joins all threads
	~ThreadPool() {
		{
			std::unique_lock<std::mutex> lock(queueMutex);
			stop = true;
		}
		condition.notify_all();

		for (std::thread& t : threads) {
			t.join();
		}
	}

	// adds a task to the queue and wakes one waiting worker
	void enqueue(std::function<void()> task) {
		{
			std::unique_lock<std::mutex> lock(queueMutex);
			tasks.push(std::move(task));
		}
		condition.notify_one();
	}

	// a pool owns running threads, so it must not be copied
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// every worker thread runs this function for the whole life of the pool
	void workerThread() {
		while (true) {
			std::function<void()> task;

			{
				std::unique_lock<std::mutex> lock(queueMutex);

				// sleep until there is a task to run or the pool is shutting down
				condition.wait(lock, [this] { return stop || !tasks.empty(); });

				// shutting down and nothing left to do, so this worker is finished
				if (stop && tasks.empty()) {
					return;
				}

				task = std::move(tasks.front());
				tasks.pop();
			}

			// the lock is released before the task runs, so other workers can run at the same time
			task();
		}
	}

	std::vector<std::thread> threads;
	std::queue<std::function<void()>> tasks;
	std::mutex queueMutex;
	std::condition_variable condition;
	std::atomic<bool> stop;
};