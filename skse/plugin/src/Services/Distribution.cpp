#include "Services/Services.h"

#include "Services/ServiceData.h"

// Tome distribution (OUTLINE "How effects are learned"): in memory only, at kDataLoaded, so no
// vanilla list or container record is edited and loot/vendor mods don't conflict.
//
// - Loot: the generator writes LA_TomeLoot_<vanilla list> leveled lists and maps each to its
//   vanilla LVLI in variants.json "lootInjection" (docs/dev/GENERATOR.md). Each of ours is added
//   as one entry (level 1, count 1) of the vanilla list; ours computes its own level gating
//   ("Calculate from all levels <= PC level").
// - Vendors: LA_VendorTomes_<Morrowind school> (Use All) is added to the base container (CONT)
//   of each spellmaker's merchant chest for each of their specialties; merchant chests restock
//   from their base container, so the tomes appear at the next vendor reset.
//
// Nothing is saved; the injection is redone every game start. Turning bTomes off in the MCM
// takes effect at the next game start.

namespace LA::Services
{
	namespace
	{
		// Appends a_form to a leveled list, keeping the entries sorted by level (the engine walks
		// them in level order). Returns false when already present or the list is full (LLCT is a
		// byte). VERIFY(in-game): the list rolls the new entry (e.g. `player.additem` of the
		// vanilla list, or a chest using it).
		bool AddLeveledEntry(RE::TESLeveledList* a_list, RE::TESForm* a_form, std::uint16_t a_level)
		{
			if (!a_list || !a_form) {
				return false;
			}
			auto&       entries = a_list->entries;
			const auto size = entries.size();
			for (const auto& entry : entries) {
				if (entry.form == a_form) {
					return false;
				}
			}
			if (size >= 255) {
				return false;
			}
			entries.resize(size + 1);
			auto& added = entries[size];
			added.form = a_form;
			added.count = 1;
			added.level = a_level;
			added.pad0C = 0;
			added.itemExtra = nullptr;
			std::stable_sort(entries.begin(), entries.end(), [](const RE::LEVELED_OBJECT& a_lhs, const RE::LEVELED_OBJECT& a_rhs) {
				return a_lhs.level < a_rhs.level;
			});
			a_list->numEntries = static_cast<std::uint8_t>(size + 1);
			return true;
		}

		std::size_t InjectLoot()
		{
			const auto& state = State::Get();
			const auto& data = ServiceData::Get();
			std::size_t added = 0;

			if (!data.lootInjection.empty()) {
				for (const auto& [ours, target] : data.lootInjection) {
					auto* list = state.forms.Get<RE::TESLevItem>(ours);
					auto* vanilla = FormMap::Resolve<RE::TESLevItem>(target);
					if (!list || !vanilla) {
						logger::warn("tomes: loot injection {} -> {} unresolved"sv, ours, target.ToString());
						continue;
					}
					added += AddLeveledEntry(vanilla, list, 1) ? 1 : 0;
				}
				return added;
			}

			// Fallback without variants.json: each tome book straight into its lootLists.
			for (const auto& tome : data.tomes) {
				if (!tome.loot) {
					continue;
				}
				auto* book = state.forms.Get(tome.bookEditorId);
				for (const auto& ref : tome.lootLists) {
					added += AddLeveledEntry(FormMap::Resolve<RE::TESLevItem>(ref), book, tome.level) ? 1 : 0;
				}
			}
			return added;
		}

		std::size_t InjectVendors()
		{
			const auto& state = State::Get();
			std::size_t added = 0;
			for (const auto& spellmaker : state.content.Spellmakers()) {
				if (!spellmaker.merchantChest.Valid()) {
					continue;  // no vendor chest (Elder Othreloth)
				}
				auto* chest = FormMap::Resolve<RE::TESObjectCONT>(spellmaker.merchantChest);
				if (!chest) {
					logger::warn("tomes: {} merchant chest {} is not a loaded CONT"sv, spellmaker.id, spellmaker.merchantChest.ToString());
					continue;
				}
				for (const auto& specialty : spellmaker.specialties) {
					auto* list = state.forms.Get<RE::TESLevItem>("LA_VendorTomes_" + specialty);
					if (!list) {
						continue;
					}
					bool present = false;
					chest->ForEachContainerObject([&](RE::ContainerObject& a_object) {
						present = present || a_object.obj == list;
						return present ? RE::BSContainer::ForEachResult::kStop : RE::BSContainer::ForEachResult::kContinue;
					});
					if (!present && chest->AddObjectToContainer(list, 1, nullptr)) {
						++added;
					}
				}
			}
			return added;
		}
	}

	void InjectVendorsAndLoot()
	{
		const auto& state = State::Get();
		if (!state.settings.tomes) {
			logger::info("tomes: distribution off (bTomes=0)"sv);
			return;
		}
		static std::once_flag once;  // kDataLoaded fires once, but be safe against double calls
		std::call_once(once, []() {
			const auto loot = InjectLoot();
			const auto vendors = InjectVendors();
			logger::info("tomes: {} loot list entries, {} vendor chest entries added"sv, loot, vendors);
		});
	}
}
