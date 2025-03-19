#pragma once

#include <functional>
#include <filesystem>
#include <thread>
#include <atomic>

typedef std::function<void(const std::string& filename)> FileCallbackFunc;

class DirectoryWatcher
{
public:
	DirectoryWatcher() = default;
	DirectoryWatcher(const std::string& watchDirectory, std::chrono::milliseconds pollingDelay, FileCallbackFunc callback);
	~DirectoryWatcher();

	void AddExtensionFilter(const std::string& extension);

	void Start();
	void Stop();

private:
	void PollingLoop();

	// Returns false if dir entry has extension that exists in filters.
	bool FilterFileByExtension(const std::filesystem::directory_entry& dirEntry);

	void InitializeInternalFileMapping();

private:
	std::string m_watchDirectory;
	std::thread m_watcherThread;
	std::atomic<bool> m_isWatching = true;
	std::chrono::milliseconds m_pollingDelay;


	FileCallbackFunc m_callback;
	std::set<std::string> m_fileExtensionFilter; // Will only let through files that has extension in this set.
	std::unordered_map<std::string, std::chrono::system_clock::duration> m_fileModificationTime;
	
};