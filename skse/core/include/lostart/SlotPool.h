#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace LA
{
	inline constexpr std::uint16_t kPrimarySlots = 500;
	inline constexpr std::uint16_t kSubSlots = 500;

	// Allocates the fixed primary and sub-spell slots (OUTLINE "Why fixed slots").
	class SlotPool
	{
	public:
		explicit SlotPool(std::uint16_t a_primary = kPrimarySlots, std::uint16_t a_sub = kSubSlots);

		std::optional<std::uint16_t> AllocatePrimary();
		std::optional<std::uint16_t> AllocateSub();
		// Allocates a_count sub slots atomically; empty result when not enough are free.
		std::vector<std::uint16_t> AllocateSubs(std::size_t a_count);

		bool ClaimPrimary(std::uint16_t a_slot);  // used when rebuilding from the co-save
		bool ClaimSub(std::uint16_t a_slot);
		void FreePrimary(std::uint16_t a_slot);
		void FreeSub(std::uint16_t a_slot);
		void Reset();

		bool          PrimaryUsed(std::uint16_t a_slot) const { return a_slot < _primary.size() && _primary[a_slot]; }
		bool          SubUsed(std::uint16_t a_slot) const { return a_slot < _sub.size() && _sub[a_slot]; }
		std::size_t   FreePrimaryCount() const;
		std::size_t   FreeSubCount() const;
		std::size_t   UsedPrimaryCount() const { return _primary.size() - FreePrimaryCount(); }
		std::size_t   UsedSubCount() const { return _sub.size() - FreeSubCount(); }
		std::uint16_t PrimaryCapacity() const { return static_cast<std::uint16_t>(_primary.size()); }
		std::uint16_t SubCapacity() const { return static_cast<std::uint16_t>(_sub.size()); }

	private:
		std::vector<bool> _primary;
		std::vector<bool> _sub;
	};
}
