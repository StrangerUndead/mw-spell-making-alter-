#include "Services/Services.h"

#include "Services/Altars.h"
#include "Services/ServiceData.h"
#include "Services/ServicesInternal.h"
#include "UI/MenuModel.h"
#include "UI/SpellmakingMenu.h"

// Where spells are made (OUTLINE "Where spells are made"): the service entry points, refusal
// rules and the event sinks. Altars live in Altars.cpp, the purchase in Purchase.cpp, tome
// distribution in Distribution.cpp.
//
// Dialogue (docs/dev/GENERATOR.md "Dialogue"): LA_Topic_MakeSpell has the shared serve INFO
// LA_Info_MakeSpell_Serve (Goodbye flag, LA_ServiceRefusal == 0), per-NPC refusal INFOs
// LA_Info_MakeSpell_Refuse_<Spellmaker> (0 < LA_ServiceRefusal < 4) and the generic
// LA_Info_MakeSpell_Refuse (>= 4). No INFO has a script: the DLL keeps LA_ServiceRefusal set for
// the NPC the player is about to talk to and opens the menu when the serve INFO ends.

namespace LA::Services
{
	namespace
	{
		std::mutex                                   g_factsMutex;
		std::function<void(Mech::RefusalFacts&)>     g_factsOverride;

		RE::TESGlobal* RefusalGlobal()
		{
			static RE::TESGlobal* global = State::Get().forms.Get<RE::TESGlobal>("LA_ServiceRefusal");
			return global;
		}

		bool IsServeInfo(RE::FormID a_info)
		{
			static const auto* serve = State::Get().forms.Get("LA_Info_MakeSpell_Serve");
			return serve && serve->GetFormID() == a_info;
		}

		bool IsRefuseInfo(RE::FormID a_info)
		{
			return State::Get().forms.EditorIdOf(a_info).starts_with("LA_Info_MakeSpell_Refuse");
		}

		const SpellmakerExtra* ExtraFor(const Spellmaker* a_spellmaker)
		{
			if (!a_spellmaker) {
				return nullptr;
			}
			const auto& extras = ServiceData::Get().spellmakers;
			const auto  it = extras.find(a_spellmaker->id);
			return it != extras.end() ? &it->second : nullptr;
		}

		RE::TESFaction* CrimeFactionFor(const Spellmaker& a_spellmaker)
		{
			if (a_spellmaker.crimeFaction.Valid()) {
				return FormMap::Resolve<RE::TESFaction>(a_spellmaker.crimeFaction);
			}
			if (const auto* extra = ExtraFor(&a_spellmaker); extra && !extra->hold.empty()) {
				const auto& holds = ServiceData::Get().holdCrimeFactions;
				if (const auto it = holds.find(extra->hold); it != holds.end()) {
					return FormMap::Resolve<RE::TESFaction>(it->second);
				}
			}
			return nullptr;
		}

		// Player's relationship rank with an NPC, Papyrus scale (-4 archnemesis .. 4 lover; 0
		// acquaintance, also when no relationship record exists).
		int RelationshipRank(RE::TESNPC* a_npc)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* playerBase = player ? player->GetActorBase() : nullptr;
			if (!a_npc || !playerBase) {
				return 0;
			}
			// VERIFY(in-game): BGSRelationship::GetRelationship finds runtime relationships the
			// game creates with the player (Favor quests, SetRelationshipRank) in either order.
			const auto* relationship = RE::BGSRelationship::GetRelationship(a_npc, playerBase);
			if (!relationship) {
				relationship = RE::BGSRelationship::GetRelationship(playerBase, a_npc);
			}
			if (!relationship) {
				return 0;
			}
			return 4 - static_cast<int>(relationship->level.get());
		}

		// --- Event sinks ------------------------------------------------------------------

		class ServiceEvents final :
			public RE::BSTEventSink<RE::TESFurnitureEvent>,
			public RE::BSTEventSink<RE::TESTopicInfoEvent>,
			public RE::BSTEventSink<RE::TESActivateEvent>,
			public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>,
			public RE::BSTEventSink<RE::TESLoadGameEvent>,
			public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
			public RE::BSTEventSink<SKSE::CrosshairRefEvent>,
			public RE::BSTEventSink<SKSE::ModCallbackEvent>
		{
		public:
			static ServiceEvents* Get()
			{
				static ServiceEvents instance;
				return &instance;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESFurnitureEvent* a_event, RE::BSTEventSource<RE::TESFurnitureEvent>*) override
			{
				if (!a_event || !a_event->actor || !a_event->actor->IsPlayerRef()) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* furniture = a_event->targetFurniture.get();
				if (a_event->type == RE::TESFurnitureEvent::FurnitureEventType::kEnter && IsAltar(furniture)) {
					RE::ObjectRefHandle handle = furniture->CreateRefHandle();
					SKSE::GetTaskInterface()->AddTask([handle]() {
						if (auto ref = handle.get()) {
							OpenSpellmaking(ref.get());
						}
					});
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESTopicInfoEvent* a_event, RE::BSTEventSource<RE::TESTopicInfoEvent>*) override
			{
				if (!a_event || a_event->type != RE::TESTopicInfoEvent::TopicInfoEventType::kTopicEnd) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* speaker = a_event->speakerRef ? a_event->speakerRef->As<RE::Actor>() : nullptr;
				const auto info = a_event->topicInfoFormID;
				if (IsServeInfo(info) && speaker) {
					RE::ObjectRefHandle handle = speaker->CreateRefHandle();
					// The serve INFO has the Goodbye flag: the dialogue menu is closing. Queue the
					// open so it lands after the close in the UI message queue.
					SKSE::GetTaskInterface()->AddTask([handle]() {
						if (auto ref = handle.get()) {
							OpenSpellmaking(ref.get());
						}
					});
				} else if (IsRefuseInfo(info) && speaker) {
					// OUTLINE "Messages beyond Morrowind": the College masters' spoken refusal is
					// followed by the membership notice; other refusals were said in the line.
					const auto* spellmaker = FindSpellmaker(speaker);
					if (RefusalReasonFor(spellmaker, speaker) == 1) {
						UI::ShowMessage(RefusalMessage(spellmaker, 1));
					}
				} else if (speaker) {
					UpdateRefusalGlobal(speaker);  // conditions are re-evaluated for the next topic list
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* a_event, RE::BSTEventSource<RE::TESActivateEvent>*) override
			{
				if (a_event && a_event->actionRef && a_event->actionRef->IsPlayerRef() && a_event->objectActivated) {
					if (auto* actor = a_event->objectActivated->As<RE::Actor>()) {
						UpdateRefusalGlobal(actor);
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESCellFullyLoadedEvent* a_event, RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override
			{
				if (a_event && a_event->cell) {
					const auto formId = a_event->cell->GetFormID();
					// Deferred: creating references from inside the cell-load notification is
					// avoided; the task runs on the main thread in the next frame.
					SKSE::GetTaskInterface()->AddTask([formId]() {
						if (auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(formId)) {
							PlaceAltarsIn(cell);
						}
					});
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESLoadGameEvent*, RE::BSTEventSource<RE::TESLoadGameEvent>*) override
			{
				SKSE::GetTaskInterface()->AddTask([]() { PlaceAltars(); });
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (a_event && a_event->opening && a_event->menuName == RE::DialogueMenu::MENU_NAME) {
					if (auto* topics = RE::MenuTopicManager::GetSingleton()) {
						auto speaker = topics->speaker.get();
						if (auto* actor = speaker ? speaker->As<RE::Actor>() : nullptr) {
							UpdateRefusalGlobal(actor);
						}
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const SKSE::CrosshairRefEvent* a_event, RE::BSTEventSource<SKSE::CrosshairRefEvent>*) override
			{
				// VERIFY(in-game): LA_ServiceRefusal is set before the topic list is evaluated.
				// Looking at an NPC always precedes talking to them, so the global is right before
				// the topic list is built (activation and dialogue-open updates are backups).
				if (a_event && a_event->crosshairRef) {
					if (auto* actor = a_event->crosshairRef->As<RE::Actor>(); actor && FindSpellmaker(actor)) {
						UpdateRefusalGlobal(actor);
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* a_event, RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
			{
				// main.cpp reloads the settings on the same event through RunOnMainThread; two task
				// hops put the altar refresh after that reload whatever the sink order.
				if (a_event && a_event->eventName == "LostArt_SettingsChanged"sv) {
					SKSE::GetTaskInterface()->AddTask([]() { SKSE::GetTaskInterface()->AddTask([]() { ApplyAltarState(); }); });
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	// --- Spellmakers ------------------------------------------------------------------------

	const Spellmaker* FindSpellmaker(const RE::Actor* a_actor)
	{
		if (!a_actor) {
			return nullptr;
		}
		const auto* base = a_actor->GetActorBase();
		if (!base) {
			return nullptr;
		}
		static std::once_flag                                    once;
		static std::unordered_map<RE::FormID, const Spellmaker*> byNpc;
		std::call_once(once, []() {
			for (const auto& spellmaker : State::Get().content.Spellmakers()) {
				if (auto* npc = FormMap::Resolve<RE::TESNPC>(spellmaker.npc)) {
					byNpc.emplace(npc->GetFormID(), &spellmaker);
				}
			}
		});
		const auto it = byNpc.find(base->GetFormID());
		return it != byNpc.end() ? it->second : nullptr;
	}

	const Spellmaker* FindSpellmakerById(std::string_view a_id)
	{
		for (const auto& spellmaker : State::Get().content.Spellmakers()) {
			if (spellmaker.id == a_id) {
				return &spellmaker;
			}
		}
		return nullptr;
	}

	bool PlayerInFaction(const FormRef& a_faction)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* faction = FormMap::Resolve<RE::TESFaction>(a_faction);
		return player && faction && player->IsInFaction(faction);
	}

	bool PlayerIsArchMage()
	{
		return PlayerInFaction(FormRef{ "Skyrim.esm", Vanilla::kCollegeArchMageFaction });
	}

	Mech::RefusalFacts GatherFacts(const Spellmaker* a_spellmaker, RE::Actor* a_actor)
	{
		const auto&        state = State::Get();
		Mech::RefusalFacts facts;
		facts.servicesEnabled = state.settings.spellmakers;
		facts.collegeMember = PlayerInFaction(ServiceData::Get().collegeFaction);

		if (a_spellmaker) {
			if (a_spellmaker->rule == Mech::RefusalRule::kWanted) {
				if (auto* faction = CrimeFactionFor(*a_spellmaker)) {
					facts.wantedInHold = faction->GetCrimeGold() > 0;
				}
			}
			if (a_spellmaker->rule == Mech::RefusalRule::kQuest) {
				// A missing quest means its DLC is absent, and so is the spellmaker: no refusal.
				if (auto* quest = FormMap::Resolve<RE::TESQuest>(a_spellmaker->quest)) {
					facts.questDone = quest->IsCompleted() || quest->GetCurrentStageID() >= a_spellmaker->questStage;
				}
			}
		}

		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			for (const auto id : { Vanilla::kAbVampire04, Vanilla::kAbVampire04b }) {
				auto* ability = RE::TESForm::LookupByID<RE::SpellItem>(id);
				if (ability && player->HasSpell(ability)) {
					facts.vampireStage4 = true;
				}
			}
		}

		RE::TESNPC* npc = a_actor ? a_actor->GetActorBase() : nullptr;
		if (!npc && a_spellmaker) {
			npc = FormMap::Resolve<RE::TESNPC>(a_spellmaker->npc);
		}
		facts.relationshipRank = RelationshipRank(npc);

		std::scoped_lock lock(g_factsMutex);
		if (g_factsOverride) {
			g_factsOverride(facts);
		}
		return facts;
	}

	int RefusalReasonFor(const Spellmaker* a_spellmaker, RE::Actor* a_actor)
	{
		const auto facts = GatherFacts(a_spellmaker, a_actor);
		const auto rule = a_spellmaker ? a_spellmaker->rule : Mech::RefusalRule::kNone;
		return Mech::RefusalReason(rule, facts, State::Get().settings);
	}

	void SetFactsOverride(std::function<void(Mech::RefusalFacts&)> a_override)
	{
		std::scoped_lock lock(g_factsMutex);
		g_factsOverride = std::move(a_override);
	}

	std::string RefusalMessage(const Spellmaker* a_spellmaker, int a_reason)
	{
		const auto& strings = State::Get().strings;
		switch (a_reason) {
		case 1:
			return strings.Get("$LA_Msg_SpellmakerMembersOnly");
		case 2:
		case 3:
			if (const auto* extra = ExtraFor(a_spellmaker); extra && !extra->refusalLine.empty() && strings.Has(extra->refusalLine)) {
				// Already spoken by LA_Info_MakeSpell_Refuse_<Spellmaker>; repeated only when the
				// service was requested outside dialogue (Papyrus OpenSpellmaking).
				return strings.Get(extra->refusalLine);
			}
			return {};
		default:
			return {};
		}
	}

	void UpdateRefusalGlobal(RE::Actor* a_actor)
	{
		auto* global = RefusalGlobal();
		if (!global || !a_actor || a_actor->IsPlayerRef()) {
			return;
		}
		const auto* spellmaker = FindSpellmaker(a_actor);
		if (!spellmaker) {
			return;
		}
		const int reason = RefusalReasonFor(spellmaker, a_actor);
		if (global->value != static_cast<float>(reason)) {
			logger::debug("services: LA_ServiceRefusal = {} for {}"sv, reason, spellmaker->id);
		}
		global->value = static_cast<float>(reason);
	}

	// --- Public API (Services.h) ------------------------------------------------------------

	void Install()
	{
		auto* events = ServiceEvents::Get();
		if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESFurnitureEvent>(events);
			holder->AddEventSink<RE::TESTopicInfoEvent>(events);
			holder->AddEventSink<RE::TESActivateEvent>(events);
			holder->AddEventSink<RE::TESCellFullyLoadedEvent>(events);
			holder->AddEventSink<RE::TESLoadGameEvent>(events);
		}
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(events);
		}
		if (auto* crosshair = SKSE::GetCrosshairRefEventSource()) {
			crosshair->AddEventSink(events);
		}
		if (auto* modEvents = SKSE::GetModCallbackEventSource()) {
			modEvents->AddEventSink(events);
		}
		(void)ServiceData::Get();  // parse the JSON extras now, not on the first dialogue
		// Install runs at kDataLoaded, which is when the tome lists must be injected.
		InjectVendorsAndLoot();
		UI::RegisterParityTests();
		logger::info("services installed ({} spellmakers, {} altars)"sv, State::Get().content.Spellmakers().size(),
			State::Get().content.Altars().size());
	}

	int RefusalReason(RE::Actor* a_spellmaker)
	{
		if (!a_spellmaker) {
			return 6;
		}
		// Actors not in spellmakers.json (another mod's spellmaker through Papyrus) get the
		// universal rules only (reasons 4, 5, 6).
		return RefusalReasonFor(FindSpellmaker(a_spellmaker), a_spellmaker);
	}

	Provider MakeProvider(RE::TESObjectREFR* a_provider)
	{
		const auto& state = State::Get();
		Provider    provider;
		if (!a_provider) {
			provider.kind = Provider::Kind::kAltar;
			provider.name = state.strings.Get("$LA_UI_AltarTitle");
			return provider;
		}
		provider.formId = a_provider->GetFormID();
		if (auto* actor = a_provider->As<RE::Actor>()) {
			provider.kind = Provider::Kind::kSpellmaker;
			const char* name = actor->GetDisplayFullName();
			provider.name = name ? name : "";
			if (const auto* spellmaker = FindSpellmaker(actor)) {
				provider.priceMult = spellmaker->priceMult;
				if (provider.name.empty()) {
					provider.name = spellmaker->name;
				}
			}
			return provider;
		}
		provider.kind = Provider::Kind::kAltar;
		const char* name = a_provider->GetDisplayFullName();
		provider.name = name && *name ? name : state.strings.Get("$LA_UI_AltarTitle");
		if (const auto* altar = AltarFor(a_provider)) {
			const auto& extras = ServiceData::Get().altars;
			if (const auto it = extras.find(altar->id); it != extras.end()) {
				if (!it->second.nameKey.empty() && state.strings.Has(it->second.nameKey)) {
					provider.name = state.strings.Get(it->second.nameKey);
				}
				// OUTLINE "Price": the Arch-Mage uses the College altar free.
				if (altar->freeFor == "archmage") {
					const auto& faction = it->second.freeForFaction;
					provider.freeService = faction.Valid() ? PlayerInFaction(faction) : PlayerIsArchMage();
				}
			} else if (altar->freeFor == "archmage") {
				provider.freeService = PlayerIsArchMage();
			}
		}
		return provider;
	}

	void OpenSpellmaking(RE::TESObjectREFR* a_provider)
	{
		const auto& state = State::Get();
		if (!state.dataReady) {
			logger::warn("services: OpenSpellmaking before data is ready"sv);
			return;
		}
		if (a_provider) {
			if (auto* actor = a_provider->As<RE::Actor>()) {
				const auto* spellmaker = FindSpellmaker(actor);
				const int   reason = RefusalReasonFor(spellmaker, actor);
				if (reason != 0) {
					logger::info("services: {} refuses (reason {})"sv, spellmaker ? spellmaker->id : "actor", reason);
					if (const auto text = RefusalMessage(spellmaker, reason); !text.empty()) {
						UI::ShowMessage(text);
					}
					return;
				}
			} else if (const auto* altar = AltarFor(a_provider)) {
				if (!state.settings.altars) {
					StandUpFromAltar(a_provider);
					return;
				}
				if (altar->access == "college" && state.settings.collegeMembership && !PlayerInFaction(ServiceData::Get().collegeFaction)) {
					UI::ShowMessage(state.strings.Get("$LA_Msg_AltarMembersOnly"));
					StandUpFromAltar(a_provider);
					return;
				}
			}
		}
		UI::OpenSpellmakingMenu(MakeProvider(a_provider), a_provider);
	}

	std::uint32_t Haggle(std::uint32_t a_price, RE::Actor* a_merchant)
	{
		// Skyrim's buy price (UESP "Skyrim:Speech", Prices): value x (fBarterMax - (fBarterMax -
		// fBarterMin) x min(Speech, 100) / 100), less Fortify Barter (SpeechcraftPowerModifier,
		// VERIFY(in-game)), then the Modify Buy Prices perk entry point (Haggling, Allure).
		// Our flat fee plays the part of the value at Speech 0, so it is scaled by the factor
		// relative to fBarterMax: haggling can only lower the fee.
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || a_price == 0) {
			return a_price;
		}
		float barterMin = 2.0f;
		float barterMax = 3.3f;
		if (auto* settings = RE::GameSettingCollection::GetSingleton()) {
			if (auto* setting = settings->GetSetting("fBarterMin")) {
				barterMin = setting->GetFloat();
			}
			if (auto* setting = settings->GetSetting("fBarterMax")) {
				barterMax = setting->GetFloat();
			}
		}
		auto*       avOwner = player->AsActorValueOwner();
		const float speech = std::clamp(avOwner ? avOwner->GetActorValue(RE::ActorValue::kSpeech) : 0.0f, 0.0f, 100.0f);
		const float fortify = std::clamp(avOwner ? avOwner->GetActorValue(RE::ActorValue::kSpeechcraftPowerModifier) : 0.0f, 0.0f, 99.0f);
		float       factor = barterMax > 0.0f ? (barterMax - (barterMax - barterMin) * speech / 100.0f) / barterMax : 1.0f;
		factor *= 1.0f - fortify / 100.0f;

		// Mod Buy Prices: filters are Perk Owner, Item, Speaker (CK "Perk Entry Point").
		// VERIFY(in-game): argument order (item, speaker) of the variadic entry-point call.
		float        perkMult = 1.0f;
		RE::TESForm* gold = RE::TESForm::LookupByID(Vanilla::kGold001);
		RE::Actor*   speaker = a_merchant ? a_merchant : player;
		RE::BGSEntryPoint::HandleEntryPoint(RE::BGSEntryPoint::ENTRY_POINT::kModBuyPrices, player, gold, speaker, &perkMult);
		factor *= std::clamp(perkMult, 0.1f, 1.0f);

		const auto haggled = static_cast<std::uint32_t>(std::floor(static_cast<double>(a_price) * factor));
		return std::clamp<std::uint32_t>(haggled, 1, a_price);
	}
}
