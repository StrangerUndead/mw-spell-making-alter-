#include "lostart/SlotPool.h"

#include <algorithm>

namespace LA
{
	SlotPool::SlotPool(std::uint16_t a_primary, std::uint16_t a_sub) :
		_primary(a_primary, false), _sub(a_sub, false)
	{}

	namespace
	{
		std::optional<std::uint16_t> Take(std::vector<bool>& a_bits)
		{
			for (std::size_t i = 0; i < a_bits.size(); ++i) {
				if (!a_bits[i]) {
					a_bits[i] = true;
					return static_cast<std::uint16_t>(i);
				}
			}
			return std::nullopt;
		}

		bool Claim(std::vector<bool>& a_bits, std::uint16_t a_slot)
		{
			if (a_slot >= a_bits.size() || a_bits[a_slot]) {
				return false;
			}
			a_bits[a_slot] = true;
			return true;
		}
	}

	std::optional<std::uint16_t> SlotPool::AllocatePrimary() { return Take(_primary); }
	std::optional<std::uint16_t> SlotPool::AllocateSub() { return Take(_sub); }

	std::vector<std::uint16_t> SlotPool::AllocateSubs(std::size_t a_count)
	{
		if (FreeSubCount() < a_count) {
			return {};
		}
		std::vector<std::uint16_t> out;
		for (std::size_t i = 0; i < a_count; ++i) {
			out.push_back(*Take(_sub));
		}
		return out;
	}

	bool SlotPool::ClaimPrimary(std::uint16_t a_slot) { return Claim(_primary, a_slot); }
	bool SlotPool::ClaimSub(std::uint16_t a_slot) { return Claim(_sub, a_slot); }

	void SlotPool::FreePrimary(std::uint16_t a_slot)
	{
		if (a_slot < _primary.size()) {
			_primary[a_slot] = false;
		}
	}

	void SlotPool::FreeSub(std::uint16_t a_slot)
	{
		if (a_slot < _sub.size()) {
			_sub[a_slot] = false;
		}
	}

	void SlotPool::Reset()
	{
		std::fill(_primary.begin(), _primary.end(), false);
		std::fill(_sub.begin(), _sub.end(), false);
	}

	std::size_t SlotPool::FreePrimaryCount() const { return static_cast<std::size_t>(std::count(_primary.begin(), _primary.end(), false)); }
	std::size_t SlotPool::FreeSubCount() const { return static_cast<std::size_t>(std::count(_sub.begin(), _sub.end(), false)); }
}
