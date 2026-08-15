#include "http/http.h"
#include "threading/queue.h"
#include "util/util.h"
#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

namespace raidhook
{
	namespace
	{
		struct HTTPEvent
		{
			std::unique_ptr<HTTPItem> completedItem;
			HTTPItem* item = nullptr;
			int64_t byteProgress = 0;
			int64_t byteTotal = 0;
		};
	} // namespace

	using HTTPEventPtr = std::unique_ptr<HTTPEvent>;
	RAIDHOOK_REGISTER_EVENTQUEUE(HTTPEventPtr, HTTPEvent)

	HTTPManager::HTTPManager()
	{
		if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK)
			RAIDHOOK_LOG_ERROR("cURL initialisation failed");

		workers.reserve(4);
		for (int i = 0; i < 4; ++i)
			workers.emplace_back([this]() { RunWorker(); });
		RAIDHOOK_LOG_LOG("cURL initialised");
	}

	HTTPManager::~HTTPManager()
	{
		{
			std::lock_guard lock(requestMutex);
			stopping = true;
		}
		requestCondition.notify_all();
		for (std::thread& worker : workers)
			worker.join();

		RAIDHOOK_LOG_LOG("cURL shut down");
		curl_global_cleanup();
	}

	HTTPManager* HTTPManager::GetSingleton()
	{
		// The loader DLL stays resident for the life of the game. Avoid joining
		// worker threads from CRT teardown while Windows holds the loader lock.
		static HTTPManager* httpSingleton = new HTTPManager();
		return httpSingleton;
	}

	size_t write_http_header(char* ptr, size_t size, size_t nmemb, void* data)
	{
		HTTPItem* mainItem = (HTTPItem*)data;
		size_t headerLineSize = size * nmemb;
		std::string_view headerLine(ptr, headerLineSize);
		size_t delimiterPosition = headerLine.find(": ");
		if (delimiterPosition != std::string::npos)
		{
			std::string headerKey(headerLine.substr(0, delimiterPosition));
			std::transform(headerKey.begin(), headerKey.end(), headerKey.begin(),
			               [](unsigned char c) { return std::tolower(c); });
			std::string_view headerValue = headerLine.substr(delimiterPosition + 2);
			if (headerValue.ends_with("\r\n"))
				headerValue.remove_suffix(2);
			mainItem->responseHeaders.insert_or_assign(std::move(headerKey), std::string(headerValue));
		}
		return headerLineSize;
	}

	size_t write_http_data(char* ptr, size_t size, size_t nmemb, void* data)
	{
		size_t byteCount = size * nmemb;
		HTTPItem* mainItem = (HTTPItem*)data;
		mainItem->httpContents.append(ptr, byteCount);
		return byteCount;
	}

	void run_http_event(std::unique_ptr<HTTPEvent> event)
	{
		RAIDHOOK_TRACE_FUNC;
		if (event->completedItem)
		{
			event->completedItem->call(event->completedItem.get());
			return;
		}

		HTTPItem* item = event->item;
		item->progress(item->data, event->byteProgress, event->byteTotal);
	}

	int http_progress_call(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
	{
		RAIDHOOK_TRACE_FUNC;
		HTTPItem* ourItem = (HTTPItem*)clientp;
		if (!ourItem->progress)
			return 0;
		if (dltotal == 0 || dlnow == 0)
			return 0;
		if (dltotal == dlnow)
			return 0;
		constexpr curl_off_t progressStep = 64 * 1024;
		if (ourItem->byteprogress >= dlnow ||
		    (ourItem->byteprogress != 0 && dlnow - ourItem->byteprogress < progressStep))
			return 0;
		ourItem->byteprogress = dlnow;
		ourItem->bytetotal = dltotal;

		auto event = std::make_unique<HTTPEvent>();
		event->item = ourItem;
		event->byteProgress = dlnow;
		event->byteTotal = dltotal;

		GetHTTPEventQueue().AddToQueue(run_http_event, std::move(event));
		return 0;
	}

	void queue_http_completion(std::unique_ptr<HTTPItem> item)
	{
		auto event = std::make_unique<HTTPEvent>();
		event->completedItem = std::move(item);
		GetHTTPEventQueue().AddToQueue(run_http_event, std::move(event));
	}

	void launch_http_request(std::unique_ptr<HTTPItem> item)
	{
		RAIDHOOK_TRACE_FUNC;
		CURL* curl = curl_easy_init();
		if (!curl)
		{
			item->errorCode = CURLE_FAILED_INIT;
			queue_http_completion(std::move(item));
			return;
		}
		curl_easy_setopt(curl, CURLOPT_URL, item->url.c_str());
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 900L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
		curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1000L);

		curl_easy_setopt(curl, CURLOPT_USERAGENT, "SuperBLT");

		if (item->progress)
		{
			curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, http_progress_call);
			curl_easy_setopt(curl, CURLOPT_XFERINFODATA, item.get());
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0);
		}

		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_http_header);
		curl_easy_setopt(curl, CURLOPT_HEADERDATA, item.get());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_http_data);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, item.get());

		item->errorCode = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &(item->httpStatusCode));
		curl_easy_cleanup(curl);

		queue_http_completion(std::move(item));
	}

	void HTTPManager::RunWorker()
	{
		while (true)
		{
			std::unique_ptr<HTTPItem> request;
			{
				std::unique_lock lock(requestMutex);
				requestCondition.wait(lock, [this]() { return stopping || !requests.empty(); });
				if (stopping && requests.empty())
					return;
				request = std::move(requests.front());
				requests.pop_front();
			}
			launch_http_request(std::move(request));
		}
	}

	void HTTPManager::LaunchHTTPRequest(std::unique_ptr<HTTPItem> callback)
	{
		RAIDHOOK_TRACE_FUNC;
		{
			std::lock_guard lock(requestMutex);
			requests.push_back(std::move(callback));
		}
		requestCondition.notify_one();
	}
} // namespace raidhook
