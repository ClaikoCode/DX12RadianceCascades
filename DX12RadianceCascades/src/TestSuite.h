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

template<typename Inputs, typename Outputs>
class TestSuite : public TestSuiteBase
{
protected:
	struct TestCase
	{
		Inputs inputs;
		Outputs outputs;
	};

public:

	bool Tick() override
	{
		if (HasCompleted())
		{
			return false;
		}

		if (m_caseRunning == false)
		{
			LOG_INFO(
				L"Running test suite {}/{} ({}%)",
				m_currentCase + 1,
				m_testCases.size(),
				(m_currentCase + 1) * 100 / m_testCases.size()
			);

			OnCaseBegin(m_testCases[m_currentCase]);
			m_caseRunning = true;

			return true;
		}

		bool caseCompleted = OnCaseTick(m_testCases[m_currentCase]);

		if (caseCompleted)
		{
			OnCaseCompleted(m_testCases[m_currentCase]);
			m_currentCase++;
			m_caseRunning = false;
		}

		return false;
	}

	bool HasCompleted() override
	{
		return m_currentCase >= m_testCases.size();
	}

	virtual void OutputTestSuiteToCSV() override { /*Empty output*/ };

protected:
	virtual void OnCaseBegin(const TestCase& testCase) = 0;
	// Should return true if case is done and false if not.
	virtual bool OnCaseTick(TestCase& testCase) = 0;
	virtual void OnCaseCompleted(TestCase& testCase) = 0;

	void AddCase(Inputs&& inputs)
	{
		m_testCases.push_back({ inputs, {} });
	}

protected:
	std::vector<TestCase> m_testCases;
	uint32_t m_currentCase;
	bool m_caseRunning;
	bool m_delayThisFrame;
};