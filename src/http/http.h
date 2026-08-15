#ifndef __HTTP_HEADER__
#define __HTTP_HEADER__

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace raidhook
{

	void download_blt();
	struct HTTPItem;

	typedef void (*HTTPCallback)(HTTPItem* httpItem);
	typedef void (*HTTPProgress)(void* data, int64_t progress, int64_t total);

	struct HTTPItem
	{
		HTTPCallback call = nullptr;
		HTTPProgress progress = nullptr;
		std::string url;
		std::string httpContents;
		std::map<std::string, std::string> responseHeaders;
		int errorCode = 0;
		long httpStatusCode = 0;
		void* data = nullptr;

		int64_t byteprogress = 0;
		int64_t bytetotal = 0;
	};

	class HTTPManager
	{
	  private:
		HTTPManager();

	  public:
		~HTTPManager();

		static HTTPManager* GetSingleton();

		void LaunchHTTPRequest(std::unique_ptr<HTTPItem> callback);

	  private:
		void RunWorker();

		std::mutex requestMutex;
		std::condition_variable requestCondition;
		std::deque<std::unique_ptr<HTTPItem>> requests;
		std::vector<std::thread> workers;
		bool stopping = false;
	};
} // namespace raidhook

#endif // __HTTP_HEADER__
