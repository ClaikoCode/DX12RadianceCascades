#include "rcpch.h"
#include "DirectoryWatcher.h"

namespace fs = std::filesystem;

void DirectoryWatcher::PollingLoop()
{
	while (m_isWatching)
	{
		for (const auto& file : fs::recursive_directory_iterator(m_watchDirectory))
		{
			if (FilterFileByExtension(file))
			{
				continue;
			}

			const std::string filePath = file.path().string();

			// Take by reference to update value if needed without requiring a second lookup.
			std::chrono::system_clock::duration& previousWriteTime = m_fileModificationTime[filePath];
			std::chrono::system_clock::duration currentWriteTime = file.last_write_time().time_since_epoch();

			if (previousWriteTime.count() < currentWriteTime.count())
			{
				LOG_DEBUG(L"File '{}' was updated. Triggering callback.", file.path().wstring());
				m_callback(file.path().string());
				previousWriteTime = currentWriteTime;
			}
		}

		std::this_thread::sleep_for(m_pollingDelay);
	}
}

bool DirectoryWatcher::FilterFileByExtension(const std::filesystem::directory_entry& dirEntry)
{
	if (m_fileExtensionFilter.size() == 0)
	{
		return false;
	}

	const std::string extension = dirEntry.path().extension().string();
	return m_fileExtensionFilter.find(extension) == m_fileExtensionFilter.end();
}

void DirectoryWatcher::InitializeInternalFileMapping()
{
	if (m_watchDirectory.empty())
	{
		LOG_ERROR(L"No watch directory set. Cannot initilize file mappings.");
		return;
	}

	// TODO: Find a way to include filters if needed.
	for (const auto& file : fs::recursive_directory_iterator(m_watchDirectory))
	{
		const std::string filePath = file.path().string();
		m_fileModificationTime[filePath] = file.last_write_time().time_since_epoch();
	}
}

void DirectoryWatcher::AddExtensionFilter(const std::string& extension)
{
	m_fileExtensionFilter.insert(extension);
}

DirectoryWatcher::DirectoryWatcher(const std::string& watchDirectory, std::chrono::milliseconds pollingDelay, FileCallbackFunc callback)
	: m_watchDirectory(watchDirectory), m_pollingDelay(pollingDelay), m_callback(callback)
{
	InitializeInternalFileMapping();
}

DirectoryWatcher::~DirectoryWatcher()
{
	Stop();
}

void DirectoryWatcher::Start()
{
	m_isWatching = true;
	m_watcherThread = std::thread(&DirectoryWatcher::PollingLoop, this);
}

void DirectoryWatcher::Stop()
{
	m_isWatching = false;
	if (m_watcherThread.joinable())
	{
		m_watcherThread.join();
	}
}
