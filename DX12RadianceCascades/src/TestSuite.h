#pragma once

#include "Logger.h"

// Non-templated baseclass to point to.
class TestSuiteBase
{
public:
	virtual ~TestSuiteBase() = default;
	// Returns true if a new test case was instantiated.
	virtual bool Tick() = 0;
	virtual bool HasCompleted() = 0;
	virtual void OutputTestSuiteToCSV() = 0;
};

template<class Inputs, class Outputs>
struct TestCase
{
	Inputs inputs;
	Outputs outputs;
};

template<class Inputs, class Outputs>
struct TestCaseContainer
{
	std::vector<TestCase<Inputs, Outputs>> testCases;

	void EmplaceTestCase(Inputs&& inputs)
	{
		testCases.emplace_back(inputs, Outputs());
	}

	TestCase<Inputs, Outputs>& operator[](uint32_t i)
	{
		return testCases.at(i);
	}

	size_t size()
	{
		return testCases.size();
	}
};

class TestSuite : public TestSuiteBase
{
public:

	bool Tick() override
	{
		if (HasCompleted())
		{
			return false;
		}

		if (m_caseRunning == false)
		{
			size_t total = GetCaseCount();
			LOG_INFO(
				L"Running test suite {}/{} ({}%)",
				m_currentCase + 1,
				total,
				(m_currentCase + 1) * 100.0f / total
			);

			OnCaseBegin(m_currentCase);
			m_caseRunning = true;

			return true;
		}

		bool caseCompleted = OnCaseTick(m_currentCase);

		if (caseCompleted)
		{
			OnCaseCompleted(m_currentCase);
			m_currentCase++;
			m_caseRunning = false;
		}

		return false;
	}

	bool HasCompleted() override
	{
		return m_currentCase >= GetCaseCount();
	}

	virtual void OutputTestSuiteToCSV() override { /*Empty output*/ };

protected:
	virtual void OnCaseBegin(uint32_t caseIndex) = 0;
	// Should return true if case is done and false if not.
	virtual bool OnCaseTick(uint32_t caseIndex) = 0;
	virtual void OnCaseCompleted(uint32_t caseIndex) = 0;
	virtual size_t GetCaseCount() = 0;

protected:
	uint32_t m_currentCase = 0;
	bool m_caseRunning = false;
	bool m_delayThisFrame = true;
};