#include "Services/Services.h"

#include "Core/Spellbook.h"
#include "Services/Altars.h"
#include "Services/ServiceData.h"
#include "Services/ServicesInternal.h"

// The purchase (parity row 19, adapted): the spell is compiled first, then the fee is taken, so
// a failed compile never costs gold. Gold goes to the spellmaker's merchant container (their
// vendor faction's VENC reference, as Morrowind's barter gold) or their inventory, or to the
// altar's owner (Neloth's inventory; the College owner is a faction, so the fee simply leaves
// the player). Then the flash, the spell-learned sound and the HUD notice.

namespace LA::Services
{
	namespace
	{
		// Visual flash per Skyrim school (vanilla Skyrim.esm effect shaders, FormKeys checked):
		// Alteration AbsorbBlueFXS, Conjuration EnchBoundSwordFXS, Destruction EnchArmorFireFXS,
		// Illusion IllusionPositiveFXS, Restoration AbsorbGreenFXS.
		constexpr std::array<RE::FormID, kSchoolCount> kSchoolShaders{ 0x0ABF08, 0x069CE8, 0x092DE7, 0x073321, 0x0ABF07 };
		constexpr float kFlashSeconds = 1.5f;

		RE::Actor* FindLoadedActor(RE::TESNPC* a_npc)
		{
			RE::Actor* found = nullptr;
			if (auto* lists = RE::ProcessLists::GetSingleton(); lists && a_npc) {
				lists->ForEachHighActor([&](RE::Actor* a_actor) {
					if (a_actor && a_actor->GetActorBase() == a_npc) {
						found = a_actor;
						return RE::BSContainer::ForEachResult::kStop;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				});
			}
			return found;
		}

		// Where the fee goes; nullptr = it just leaves the player.
		RE::TESObjectREFR* FeeRecipient(RE::TESObjectREFR* a_provider)
		{
			if (!a_provider) {
				return nullptr;
			}
			if (auto* actor = a_provider->As<RE::Actor>()) {
				if (auto* vendor = actor->GetVendorFaction(); vendor && vendor->vendorData.merchantContainer) {
					return vendor->vendorData.merchantContainer;
				}
				return actor;  // no merchant chest (Elder Othreloth): their inventory
			}
			if (const auto* altar = AltarFor(a_provider); altar && altar->owner.Valid()) {
				if (auto* npc = FormMap::Resolve<RE::TESNPC>(altar->owner)) {
					return FindLoadedActor(npc);  // Neloth, when he is loaded (he lives there)
				}
			}
			return nullptr;  // College: paid to a faction
		}

		// The cheapest filled soul gem: pre-filled gems (base soul) or ones the player filled
		// (ExtraSoul on an extra data list).
		bool TakeSmallestFilledSoulGem(RE::PlayerCharacter* a_player)
		{
			struct Choice
			{
				RE::TESBoundObject* object{ nullptr };
				RE::ExtraDataList*  extra{ nullptr };
				RE::SOUL_LEVEL      level{ RE::SOUL_LEVEL::kNone };
			};
			std::optional<Choice> best;
			const auto consider = [&](RE::TESBoundObject* a_object, RE::ExtraDataList* a_extra, RE::SOUL_LEVEL a_level) {
				if (a_level == RE::SOUL_LEVEL::kNone) {
					return;
				}
				if (!best || a_level < best->level) {
					best = Choice{ a_object, a_extra, a_level };
				}
			};

			auto inventory = a_player->GetInventory([](RE::TESBoundObject& a_object) { return a_object.IsSoulGem(); });
			for (auto& [object, data] : inventory) {
				auto& [count, entry] = data;
				if (count <= 0 || !entry) {
					continue;
				}
				int withExtra = 0;
				if (entry->extraLists) {
					for (auto* extra : *entry->extraLists) {
						if (!extra) {
							continue;
						}
						if (const auto* soul = extra->GetByType<RE::ExtraSoul>()) {
							++withExtra;
							consider(object, extra, soul->GetContainedSoul());
						}
					}
				}
				if (count > withExtra) {
					if (const auto* gem = object->As<RE::TESSoulGem>()) {
						consider(object, nullptr, gem->GetContainedSoul());
					}
				}
			}
			if (!best) {
				return false;
			}
			a_player->RemoveItem(best->object, 1, RE::ITEM_REMOVE_REASON::kRemove, best->extra, nullptr);
			return true;
		}

		void PlayFlash(RE::TESObjectREFR* a_provider, const CompilePlan& a_plan)
		{
			const auto index = static_cast<std::size_t>(a_plan.school);
			auto*      shader = index < kSchoolShaders.size() ? RE::TESForm::LookupByID<RE::TESEffectShader>(kSchoolShaders[index]) : nullptr;
			if (!shader) {
				return;
			}
			// VERIFY(in-game): the shader reads as a short flash in the school's colour.
			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				player->ApplyEffectShader(shader, kFlashSeconds);
			}
			if (a_provider && IsAltar(a_provider)) {
				a_provider->ApplyEffectShader(shader, kFlashSeconds);  // the altar flares
			}
		}
	}

	RE::SpellItem* CompletePurchase(const Purchase& a_purchase, RE::TESObjectREFR* a_provider)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(Vanilla::kGold001);
		if (!player || !gold) {
			return nullptr;
		}
		// The menu checked the gold on the UI side; make sure nothing changed since.
		if (a_purchase.price > 0 && player->GetGoldAmount() < static_cast<std::int32_t>(a_purchase.price)) {
			logger::warn("purchase: not enough gold any more ({} needed)"sv, a_purchase.price);
			return nullptr;
		}

		RE::SpellItem* spell = nullptr;
		if (a_purchase.def.slot != kNoSlot) {
			spell = Spellbook::Replace(a_purchase.def.slot, a_purchase.def, a_purchase.plan);
		} else {
			spell = Spellbook::Create(a_purchase.def, a_purchase.plan);
		}
		if (!spell) {
			logger::error("purchase: compiling '{}' failed"sv, a_purchase.def.name);
			return nullptr;
		}

		if (a_purchase.price > 0) {
			auto* recipient = FeeRecipient(a_provider);
			player->RemoveItem(gold, static_cast<std::int32_t>(a_purchase.price),
				recipient ? RE::ITEM_REMOVE_REASON::kStoreInContainer : RE::ITEM_REMOVE_REASON::kRemove, nullptr, recipient);
			RE::SendHUDMessage::ShowInventoryChangeMessage(gold, static_cast<std::int32_t>(a_purchase.price), false, false);
			logger::info("purchase: '{}' for {} gold to {:08X}"sv, a_purchase.def.name, a_purchase.price,
				recipient ? recipient->GetFormID() : 0u);
		}
		if (a_purchase.consumeSoulGem && !TakeSmallestFilledSoulGem(player)) {
			logger::warn("purchase: the soul gem fee applied but no filled soul gem was found"sv);
		}

		PlayFlash(a_provider, a_purchase.plan);
		RE::PlaySound("UISpellLearned");
		RE::SendHUDMessage::ShowHUDMessage(State::Get().strings.Get("$LA_UI_SpellLearned").c_str());
		return spell;
	}
}
