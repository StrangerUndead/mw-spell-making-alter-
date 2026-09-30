#pragma once

// In-game scripted checks (docs/dev/CONTRACTS.md section 9). Every component registers its own
// cases; `RunTests("all")` / console `la test all` runs them and writes LostArt_Tests.log.
namespace LA::Tests
{
	struct Result
	{
		bool        pass{ false };
		std::string detail;  // "expected X, got Y" on failure
	};

	using Case = std::function<Result()>;

	void Register(std::string a_suite, std::string a_name, Case a_case);
	int  Run(std::string_view a_suite);  // returns failures (-1 when the suite doesn't exist)

	inline Result Pass(std::string a_detail = {}) { return { true, std::move(a_detail) }; }
	inline Result Fail(std::string a_detail) { return { false, std::move(a_detail) }; }
	template <class A, class B>
	Result Expect(const A& a_expected, const B& a_got, std::string_view a_what = {})
	{
		if (a_expected == a_got) {
			return Pass(std::string(a_what));
		}
		return Fail(fmt::format("{}expected {}, got {}", a_what.empty() ? "" : std::string(a_what) + ": ", a_expected, a_got));
	}
}
