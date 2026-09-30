#include "UI/MenuModel.h"

#include "Core/Spellbook.h"
#include "Services/Services.h"
#include "Services/ServicesInternal.h"
#include "Tests/InGameTests.h"
#include "UI/SpellmakingMenu.h"

// In-game suite "parity" (CONTRACTS section 9): the Morrowind parity rows the menu and the
// services own, driven through MenuController::HandleIntent with the same call names and
// argument shapes the SWF sends. A recording sink stands in for the movie, so the checks read
// exactly the payloads LA_SetState / LA_ShowMessage would carry.

namespace LA::UI
{
	namespace
	{
		using Tests::Expect;
		using Tests::Fail;
		using Tests::Pass;
		using Tests::Result;

		class RecordingSink final : public MenuSink
		{
		public:
			void SetKnown(const Val& a_known) override { known = a_known; }
			void SetState(const Val& a_state) override
			{
				state = a_state;
				++states;
			}
			void SetLoadList(const Val& a_list) override { loadList = a_list; }
			void ShowMessage(const std::string& a_text) override { messages.push_back(a_text); }
			void Close() override { closed = true; }
			void PlaySound(const std::string& a_key) override { sounds.push_back(a_key); }

			std::string LastMessage() const { return messages.empty() ? std::string("<none>") : messages.back(); }

			Val                      known;
			Val                      state;
			Val                      loadList;
			std::vector<std::string> messages;
			std::vector<std::string> sounds;
			int                      states{ 0 };
			bool                     closed{ false };
		};

		Settings TestSettings()
		{
			Settings settings = State::Get().settings;
			settings.maxEffects = 8;
			settings.magnitudeCap = 100;
			settings.durationCap = 1440;
			settings.areaCap = 50;
			settings.startingRange = 0;
			settings.haggling = false;
			settings.perNPCPrices = false;
			settings.altarFee = AltarFee::kFull;
			settings.loadPricing = 0;
			settings.castingFailure = false;
			settings.globalCostMult = 1.0;
			return settings;
		}

		// Every catalog effect the menu can offer (sandbox-style), by id.
		std::set<std::string> AllUsable()
		{
			std::set<std::string> ids;
			for (const auto& def : State::Get().catalog.All()) {
				if (HasVariants(def)) {
					ids.insert(def.id);
				}
			}
			return ids;
		}

		struct Harness
		{
			RecordingSink                   sink;
			std::shared_ptr<MenuController> controller;

			explicit Harness(ControllerOverrides a_overrides = {}, Provider::Kind a_kind = Provider::Kind::kAltar)
			{
				if (!a_overrides.known) {
					a_overrides.known = AllUsable();
				}
				if (!a_overrides.settings) {
					a_overrides.settings = TestSettings();
				}
				Provider provider;
				provider.kind = a_kind;
				provider.name = "Parity";
				controller = std::make_shared<MenuController>(provider, RE::ObjectRefHandle(), &sink, std::move(a_overrides));
				controller->Start();
				Call("LA_Ready");
			}

			void Call(std::string_view a_name, std::vector<Val> a_args = {}) { controller->HandleIntent(a_name, a_args); }

			// Morrowind's add flow: pick the effect, accept the editor.
			void Add(const std::string& a_id) { Call("LA_AddEffect", { Val::Str(a_id) }); }
			void AddOk(const std::string& a_id)
			{
				Add(a_id);
				Call("LA_EditorOk");
			}
			void Set(const char* a_field, int a_value) { Call("LA_EditorSet", { Val::Str(a_field), Val::Num(a_value) }); }

			double Num(std::string_view a_key) const
			{
				const auto* v = sink.state.Get(a_key);
				return v ? v->AsNum(-999) : -999;
			}
			std::size_t Count() const
			{
				const auto* effects = sink.state.Get("effects");
				return effects ? effects->Items().size() : 0;
			}
			const Val* Editor() const
			{
				const auto* e = sink.state.Get("editor");
				return e && !e->IsNull() ? e : nullptr;
			}
		};

		std::string Msg(LA::Msg a_msg) { return State::Get().MessageText(a_msg); }

		// Plain effects (no attribute/skill picker) the tests can add, deterministic order.
		std::vector<std::string> PlainEffects(std::size_t a_count)
		{
			std::vector<std::string> ids;
			for (const auto& id : AllUsable()) {
				const auto* def = State::Get().catalog.Find(id);
				if (def && def->target == TargetKind::kNone && !def->AllowedRanges().empty()) {
					ids.push_back(id);
					if (ids.size() == a_count) {
						break;
					}
				}
			}
			return ids;
		}

		bool Usable(std::string_view a_id)
		{
			const auto* def = State::Get().catalog.Find(a_id);
			return def && HasVariants(*def);
		}

		// --- cases ------------------------------------------------------------------------

		Result MenuRegistered()
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return Fail("no UI singleton");
			}
			const bool registered = ui->menuMap.find(RE::BSFixedString(kMenuName)) != ui->menuMap.end();
			return Expect(true, registered, "menu registered");
		}

		Result ReadySendsKnownAndState()
		{
			Harness    h;
			const auto known = h.sink.known.Items().size();
			if (known != h.controller->KnownSorted().size()) {
				return Fail(fmt::format("expected {} known entries, got {}", h.controller->KnownSorted().size(), known));
			}
			if (h.sink.states != 1) {
				return Fail(fmt::format("expected 1 LA_SetState after LA_Ready, got {}", h.sink.states));
			}
			// Sorted by translated name (parity row 4).
			std::string previous;
			for (const auto& entry : h.sink.known.Items()) {
				const auto* text = entry.Get("text");
				const auto  name = text ? ToLower(text->AsStr()) : std::string{};
				if (name < previous) {
					return Fail(fmt::format("known list not sorted: '{}' after '{}'", name, previous));
				}
				previous = name;
			}
			return Pass(fmt::format("{} known effects", known));
		}

		Result NinthEffectMessage()
		{
			const auto ids = PlainEffects(9);
			if (ids.size() < 9) {
				return Fail("fewer than 9 usable plain effects");
			}
			Harness h;
			for (std::size_t i = 0; i < 8; ++i) {
				h.AddOk(ids[i]);
			}
			if (h.Count() != 8 || !h.sink.messages.empty()) {
				return Fail(fmt::format("expected 8 effects and no message, got {} and '{}'", h.Count(), h.sink.LastMessage()));
			}
			h.Add(ids[8]);
			if (auto r = Expect(Msg(LA::Msg::kMaxEffects), h.sink.LastMessage(), "sNotifyMessage28"); !r.pass) {
				return r;
			}
			// Row 5 before row 6: a duplicate at the limit still reports the limit.
			h.sink.messages.clear();
			h.Add(ids[0]);
			if (auto r = Expect(Msg(LA::Msg::kMaxEffects), h.sink.LastMessage(), "limit before duplicate"); !r.pass) {
				return r;
			}
			// Every known row is dimmed at the limit.
			const auto* dim = h.sink.state.Get("dim");
			return Expect(h.controller->KnownSorted().size(), dim ? dim->Items().size() : 0, "dimmed rows at the limit");
		}

		Result DuplicateMessage()
		{
			const auto ids = PlainEffects(1);
			if (ids.empty()) {
				return Fail("no usable plain effect");
			}
			Harness h;
			h.AddOk(ids[0]);
			h.Add(ids[0]);
			if (auto r = Expect(Msg(LA::Msg::kDuplicate), h.sink.LastMessage(), "sOnetypeEffectMessage"); !r.pass) {
				return r;
			}
			return Expect(std::size_t{ 1 }, h.Count(), "effects after duplicate");
		}

		Result BuyCheckOrder()
		{
			const auto ids = PlainEffects(1);
			if (ids.empty()) {
				return Fail("no usable plain effect");
			}
			ControllerOverrides poor;
			poor.gold = 0;
			poor.dryRunPurchase = true;
			Harness h(poor);

			h.Call("LA_Create");
			if (auto r = Expect(Msg(LA::Msg::kNoEffects), h.sink.LastMessage(), "1: sNotifyMessage30"); !r.pass) {
				return r;
			}
			h.AddOk(ids[0]);
			h.Call("LA_Create");
			if (auto r = Expect(Msg(LA::Msg::kNoName), h.sink.LastMessage(), "2: sNotifyMessage10"); !r.pass) {
				return r;
			}
			h.Call("LA_SetName", { Val::Str("Parity Test") });
			h.Call("LA_Create");
			if (auto r = Expect(Msg(LA::Msg::kNoGold), h.sink.LastMessage(), "4: sNotifyMessage18"); !r.pass) {
				return r;
			}
			if (h.sink.state.Get("canAfford") == nullptr || h.sink.state.Get("canAfford")->AsBool(true)) {
				return Fail("canAfford should be false with 0 gold");
			}

			// Zero cost (sEnchantmentMenu8): a tiny global multiplier floors the cost to 0.
			ControllerOverrides free;
			free.gold = 1000000;
			free.dryRunPurchase = true;
			free.settings = TestSettings();
			free.settings->globalCostMult = 1e-6;
			Harness z(free);
			z.AddOk(ids[0]);
			z.Call("LA_SetName", { Val::Str("Parity Test") });
			z.Call("LA_Create");
			if (auto r = Expect(Msg(LA::Msg::kZeroCost), z.sink.LastMessage(), "3: sEnchantmentMenu8"); !r.pass) {
				return r;
			}

			// With gold: the purchase goes through, at the price the readout showed.
			ControllerOverrides rich;
			rich.gold = 1000000;
			rich.dryRunPurchase = true;
			Harness ok(rich);
			ok.AddOk(ids[0]);
			ok.Call("LA_SetName", { Val::Str("Parity Test") });
			const auto shownPrice = static_cast<std::uint32_t>(ok.Num("price"));
			ok.Call("LA_Create");
			const auto purchase = ok.controller->LastDryRun();
			if (!purchase) {
				return Fail(fmt::format("expected a purchase, got message '{}'", ok.sink.LastMessage()));
			}
			return Expect(shownPrice, purchase->price, "charged price equals shown price");
		}

		Result ReadoutsUpdate()
		{
			if (!Usable("mw.fire_damage")) {
				return Fail("mw.fire_damage not usable");
			}
			Harness h;
			const auto emptyCost = h.Num("cost");
			h.Add("mw.fire_damage");
			const auto* editor = h.Editor();
			if (!editor) {
				return Fail("editor did not open");
			}
			const auto before = editor->Get("effectCost") ? editor->Get("effectCost")->AsNum() : -1;
			h.Set("max", 40);
			editor = h.Editor();
			const auto after = editor && editor->Get("effectCost") ? editor->Get("effectCost")->AsNum() : -1;
			if (!(after > before)) {
				return Fail(fmt::format("editor effectCost should rise with magnitude: {} -> {}", before, after));
			}
			const auto line = editor && editor->Get("lineText") ? editor->Get("lineText")->AsStr() : std::string{};
			if (line.find("40") == std::string::npos) {
				return Fail("lineText does not show the new magnitude: " + line);
			}
			h.Call("LA_EditorOk");
			const auto cost = h.Num("cost");
			const auto price = h.Num("price");
			if (!(cost > emptyCost) || !(price > 0)) {
				return Fail(fmt::format("readouts did not update: cost {} -> {}, price {}", emptyCost, cost, price));
			}
			return Pass(fmt::format("cost {} price {} line '{}'", cost, price, line));
		}

		// OUTLINE formulas: Fire Damage 10 pts on Touch then Frost Damage 10 pts on Target costs 15
		// in Classic; the reverse order costs 12 (Target x1.5 on the running total).
		Result ClassicOrderQuirk()
		{
			if (!Usable("mw.fire_damage") || !Usable("mw.frost_damage")) {
				return Fail("fire/frost damage not usable");
			}
			ControllerOverrides classic;
			classic.settings = TestSettings();
			classic.settings->costModel = CostModel::kClassic;
			classic.settings->targetRunningTotal = true;
			Harness h(classic);
			h.Add("mw.fire_damage");  // starts on Touch (OpenMW rule, row 13)
			h.Set("min", 10);
			h.Set("max", 10);
			h.Call("LA_EditorOk");
			h.Add("mw.frost_damage");
			h.Call("LA_EditorRange");  // Touch -> Target
			h.Set("min", 10);
			h.Set("max", 10);
			h.Call("LA_EditorOk");
			if (auto r = Expect(15.0, h.Num("cost"), "Touch then Target"); !r.pass) {
				return r;
			}
			h.Call("LA_MoveEffect", { Val::Num(1), Val::Num(-1) });
			return Expect(12.0, h.Num("cost"), "Target then Touch");
		}

		Result RangeCycleResetsArea()
		{
			if (!Usable("mw.fire_damage")) {
				return Fail("mw.fire_damage not usable");
			}
			Harness h;
			h.Add("mw.fire_damage");
			h.Set("area", 10);
			h.Call("LA_EditorRange");  // Touch -> Target
			const auto* editor = h.Editor();
			if (!editor || editor->Get("area")->AsNum() != 10) {
				return Fail("area should stay 10 on Target");
			}
			h.Call("LA_EditorRange");  // Target -> Self
			editor = h.Editor();
			if (!editor) {
				return Fail("editor closed");
			}
			if (editor->Get("hasArea")->AsBool(true)) {
				return Fail("area should be hidden on Self");
			}
			return Expect(0.0, editor->Get("area")->AsNum(-1), "area on Self");
		}

		Result PickerOpensAndCancels()
		{
			std::string target;
			for (const auto& id : AllUsable()) {
				if (const auto* def = State::Get().catalog.Find(id); def && def->target == TargetKind::kAttribute) {
					target = id;
					break;
				}
			}
			if (target.empty()) {
				return Fail("no usable attribute effect");
			}
			Harness h;
			h.Add(target);
			const auto* picker = h.sink.state.Get("picker");
			if (!picker || picker->IsNull()) {
				return Fail("picker did not open for " + target);
			}
			if (auto r = Expect(kAttributeCount, picker->Get("options")->Items().size(), "attribute options"); !r.pass) {
				return r;
			}
			h.Call("LA_EditorCancel");  // the SWF's picker cancel
			picker = h.sink.state.Get("picker");
			if (picker && !picker->IsNull()) {
				return Fail("picker still open after LA_EditorCancel");
			}
			h.Add(target);
			h.Call("LA_PickTarget", { Val::Num(0) });
			return Expect(true, h.Editor() != nullptr, "editor after picking Strength");
		}

		Result ProviderLabels()
		{
			Harness altar({}, Provider::Kind::kAltar);
			Harness npc({}, Provider::Kind::kSpellmaker);
			const auto label = [](const Harness& a_h) {
				const auto* buttons = a_h.sink.state.Get("buttons");
				const auto* create = buttons ? buttons->Get("createLabel") : nullptr;
				return create ? create->AsStr() : std::string{};
			};
			if (auto r = Expect(std::string("$LA_UI_Create"), label(altar), "altar"); !r.pass) {
				return r;
			}
			if (auto r = Expect(std::string("$LA_UI_Buy"), label(npc), "spellmaker"); !r.pass) {
				return r;
			}
			return Expect(std::string("npc"), npc.sink.state.Get("mode")->AsStr(), "mode");
		}

		Result ExitCloses()
		{
			Harness h;
			h.Call("LA_Exit");
			return Expect(true, h.sink.closed, "LA_Exit -> LA_Close");
		}

		Result DeleteProtectsRacialPower()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* race = player ? player->GetRace() : nullptr;
			auto* list = race ? race->actorEffects : nullptr;
			if (!list || !list->spells || list->numSpells == 0) {
				return Fail("player race has no spells");
			}
			for (std::uint32_t i = 0; i < list->numSpells; ++i) {
				auto* spell = list->spells[i];
				if (!spell) {
					continue;
				}
				const auto check = Spellbook::CanDelete(spell);
				if (check == Spellbook::DeleteCheck::kOk) {
					return Fail(fmt::format("racial spell {} ({:08X}) is deletable", spell->GetName(), spell->GetFormID()));
				}
			}
			return Pass(fmt::format("{} racial spells protected", list->numSpells));
		}

		Result DeletePromptText()
		{
			const auto text = SubstituteName(State::Get().strings.Get("$LA_Msg_sQuestionDeleteSpell"), "Fireball");
			if (text.find("Fireball") == std::string::npos || text.find("%s") != std::string::npos) {
				return Fail("prompt: " + text);
			}
			return Pass(text);
		}

		Result RefusalReasons()
		{
			const auto* wanted = Services::FindSpellmakerById("farengar");
			const auto* college = Services::FindSpellmakerById("tolfdir");
			const auto* quest = Services::FindSpellmakerById("neloth");
			if (!wanted || !college) {
				return Fail("farengar/tolfdir missing from spellmakers.json");
			}
			const auto& settings = State::Get().settings;
			struct Scope
			{
				~Scope() { Services::SetFactsOverride({}); }
			} scope;
			// With the service switched off in the MCM every spellmaker answers 6.
			const auto exp = [&settings](int a_reason) { return settings.spellmakers ? a_reason : 6; };
			const auto with = [](auto a_edit, const Spellmaker* a_sm) {
				Services::SetFactsOverride([a_edit](Mech::RefusalFacts& a_facts) {
					a_facts = {};
					a_edit(a_facts);
				});
				return Services::RefusalReasonFor(a_sm, nullptr);
			};

			if (auto r = Expect(exp(0), with([](auto&) {}, wanted), "clean record"); !r.pass) {
				return r;
			}
			if (auto r = Expect(exp(settings.refuseWanted ? 2 : 0), with([](auto& f) { f.wantedInHold = true; }, wanted), "wanted in Whiterun"); !r.pass) {
				return r;
			}
			if (auto r = Expect(exp(settings.collegeMembership ? 1 : 0), with([](auto& f) { f.collegeMember = false; }, college), "not a College member"); !r.pass) {
				return r;
			}
			if (auto r = Expect(exp(0), with([](auto& f) { f.collegeMember = true; }, college), "College member"); !r.pass) {
				return r;
			}
			if (quest) {
				if (auto r = Expect(exp(3), with([](auto& f) { f.questDone = false; }, quest), "Reluctant Steward not done"); !r.pass) {
					return r;
				}
			}
			if (auto r = Expect(exp(4), with([](auto& f) { f.vampireStage4 = true; }, wanted), "stage-4 vampire"); !r.pass) {
				return r;
			}
			if (auto r = Expect(exp(5), with([](auto& f) { f.relationshipRank = -1; }, wanted), "relationship below Acquaintance"); !r.pass) {
				return r;
			}
			if (auto r = Expect(6, with([](auto& f) { f.servicesEnabled = false; }, wanted), "service disabled"); !r.pass) {
				return r;
			}
			return Expect(State::Get().strings.Get("$LA_Msg_SpellmakerMembersOnly"), Services::RefusalMessage(college, 1), "reason 1 message");
		}

		Result HaggleBounds()
		{
			const auto price = Services::Haggle(100, nullptr);
			if (price < 1 || price > 100) {
				return Fail(fmt::format("expected 1..100, got {}", price));
			}
			return Pass(fmt::format("100 -> {}", price));
		}

		Result Performance()
		{
			using Clock = std::chrono::steady_clock;
			const auto   start = Clock::now();
			RecordingSink sink;
			ControllerOverrides overrides;
			overrides.known = AllUsable();
			overrides.settings = TestSettings();
			auto controller = std::make_shared<MenuController>(Provider{}, RE::ObjectRefHandle(), &sink, overrides);
			controller->Start();
			controller->HandleIntent("LA_Ready", {});
			const auto openMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

			for (const auto& id : PlainEffects(4)) {
				controller->HandleIntent("LA_AddEffect", { Val::Str(id) });
				controller->HandleIntent("LA_EditorOk", {});
			}
			const auto ids = PlainEffects(5);
			if (ids.size() == 5) {
				controller->HandleIntent("LA_AddEffect", { Val::Str(ids[4]) });
			}
			constexpr int kSteps = 50;
			const auto    stepStart = Clock::now();
			for (int i = 0; i < kSteps; ++i) {
				controller->HandleIntent("LA_EditorStep", { Val::Str("max"), Val::Num(1), Val::Bool(false) });
			}
			const auto stepMs = std::chrono::duration<double, std::milli>(Clock::now() - stepStart).count() / kSteps;
			const auto detail = fmt::format("open {:.1f} ms with {} effects (budget 150), step {:.3f} ms (budget 2)", openMs,
				controller->KnownSorted().size(), stepMs);
			logger::info("parity.performance: {}"sv, detail);
			if (openMs >= 150.0 || stepMs >= 2.0) {
				return Fail(detail);
			}
			return Pass(detail);
		}
	}

	void RegisterParityTests()
	{
		static std::once_flag once;
		std::call_once(once, []() {
			const auto reg = [](const char* a_name, Tests::Case a_case) { Tests::Register("parity", a_name, std::move(a_case)); };
			reg("menu_registered", MenuRegistered);
			reg("ready_known_state", ReadySendsKnownAndState);
			reg("row5_ninth_effect", NinthEffectMessage);
			reg("row6_duplicate", DuplicateMessage);
			reg("row18_buy_check_order", BuyCheckOrder);
			reg("row15_readouts_update", ReadoutsUpdate);
			reg("row16_classic_order", ClassicOrderQuirk);
			reg("row11_self_resets_area", RangeCycleResetsArea);
			reg("row7_picker", PickerOpensAndCancels);
			reg("provider_labels", ProviderLabels);
			reg("exit_closes", ExitCloses);
			reg("row24_racial_protected", DeleteProtectsRacialPower);
			reg("row24_delete_prompt", DeletePromptText);
			reg("row2_refusal_reasons", RefusalReasons);
			reg("haggle_bounds", HaggleBounds);
			reg("performance", Performance);
		});
	}
}
