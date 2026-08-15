#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include "lua.h"
#include "threading/queue.h"
#include "util.h"

using namespace std;

struct HashInfo
{
	lua_State* L;
	int ref;
	string filename;
	raidhook::Util::DirectoryHashFunction hasher;
	raidhook::Util::HashResultReceiver callback;

	string result;
};

RAIDHOOK_REGISTER_EVENTQUEUE(HashInfo, HashResult)

class AsyncHashManager
{
  private:
	AsyncHashManager();
	void RunWorker();

  public:
	~AsyncHashManager();

	static AsyncHashManager* GetSingleton();
	void Dispatch(HashInfo info);

  private:
	std::mutex mutex;
	std::condition_variable condition;
	std::queue<HashInfo> tasks;
	std::vector<std::thread> workers;
	bool stopping = false;
};

AsyncHashManager::AsyncHashManager()
{
	workers.reserve(2);
	for (int i = 0; i < 2; ++i)
		workers.emplace_back([this]() { RunWorker(); });
}

AsyncHashManager::~AsyncHashManager()
{
	{
		lock_guard lock(mutex);
		stopping = true;
	}
	condition.notify_all();
	for (thread& worker : workers)
		worker.join();
}

AsyncHashManager* AsyncHashManager::GetSingleton()
{
	// The DLL is process-lifetime; don't join workers during loader-lock teardown.
	static AsyncHashManager* instance = new AsyncHashManager();
	return instance;
}

static void done(HashInfo info)
{
	info.callback(info.L, info.ref, std::move(info.filename), std::move(info.result));
}

static void run_async(HashInfo info)
{
	info.result = info.hasher(info.filename);

	GetHashResultQueue().AddToQueue(done, std::move(info));
}

void AsyncHashManager::RunWorker()
{
	while (true)
	{
		HashInfo task;
		{
			unique_lock lock(mutex);
			condition.wait(lock, [this]() { return stopping || !tasks.empty(); });
			if (stopping && tasks.empty())
				return;
			task = std::move(tasks.front());
			tasks.pop();
		}
		run_async(std::move(task));
	}
}

void AsyncHashManager::Dispatch(HashInfo info)
{
	{
		lock_guard lock(mutex);
		tasks.push(std::move(info));
	}
	condition.notify_one();
}

void raidhook::Util::RunAsyncHash(lua_State* L, int ref, string filename, DirectoryHashFunction hasher,
                                  HashResultReceiver callback)
{
	HashInfo info;

	info.L = L;
	info.ref = ref;
	info.filename = std::move(filename);
	info.hasher = hasher;
	info.callback = callback;

	info.result = "<ERR:NOTSET>";

	AsyncHashManager::GetSingleton()->Dispatch(std::move(info));
}
