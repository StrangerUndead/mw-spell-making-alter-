#include "UI/MenuModel.h"

#include "Casting/CastRouter.h"
#include "Core/Spellbook.h"
#include "Services/Services.h"
#include "UI/SpellmakingMenu.h"

// MenuController: the host-independent half of the spellmaking menu. Every rule lives in the
// core MenuSession; this file only translates intents into session calls and the session into
// the CONTRACTS section 7 state object.

namespace LA::UI
{
	// --- Val --------------------------------------------------------------------------------

	Val Val::Bool(bool a_value)
	{
		Val v(Type::kBool);
		v._bool = a_value;
		return v;
	}

	Val Val::Num(double a_value)
	{
		Val v(Type::kNumber);
		v._num = a_value;
		return v;
	}

	Val Val::Str(std::string a_value)
	{
		Val v(Type::kString);
		v._str = std::move(a_value);
		return v;
	}

	Val& Val::Set(std::string a_key, Val a_value)
	{
		if (_type != Type::kObject) {
			*this = Object();
		}
		for (auto& [key, value] : _members) {
			if (key == a_key) {
				value = std::move(a_value);
				return *this;
			}
		}
		_members.emplace_back(std::move(a_key), std::move(a_value));
		return *this;
	}

	const Val* Val::Get(std::string_view a_key) const
	{
		for (const auto& [key, value] : _members) {
			if (key == a_key) {
				return &value;
			}
		}
		return nullptr;
	}

	void Val::Push(Val a_value)
	{
		if (_type != Type::kArray) {
			*this = Array();
		}
		_items.push_back(std::move(a_value));
	}

	bool Val::AsBool(bool a_default) const
	{
		switch (_type) {
		case Type::kBool:
			return _bool;
		case Type::kNumber:
			return _num != 0.0;
		case Type::kString:
			return _str == "true" || _str == "1";
		default:
			return a_default;
		}
	}

	double Val::AsNum(double a_default) const
	{
		switch (_type) {
		case Type::kNumber:
			return _num;
		case Type::kBool:
			return _bool ? 1.0 : 0.0;
		case Type::kString:
			try {
				return std::stod(_str);
			} catch (...) {
				return a_default;
			}
		default:
			return a_default;
		}
	}

	std::string Val::AsStr() const
	{
		switch (_type) {
		case Type::kString:
			return _str;
		case Type::kNumber:
			return fmt::format("{}", _num);
		case Type::kBool:
			return _bool ? "true" : "false";
		default:
			return {};
		}
	}

	std::string Val::Dump() const
	{
		switch (_type) {
		case Type::kUndefined:
			return "undefined";
		case Type::kNull:
			return "null";
		case Type::kBool:
			return _bool ? "true" : "false";
		case Type::kNumber:
			return fmt::format("{}", _num);
		case Type::kString:
			{
				std::string out = "\"";
				for (const char c : _str) {
					if (c == '"' || c == '\\') {
						out += '\\';
					}
					out += c;
				}
				return out + "\"";
			}
		case Type::kArray:
			{
				std::string out = "[";
				for (std::size_t i = 0; i < _items.size(); ++i) {
					out += (i ? "," : "") + _items[i].Dump();
				}
				return out + "]";
			}
		case Type::kObject:
			{
				std::string out = "{";
				for (std::size_t i = 0; i < _members.size(); ++i) {
					out += (i ? ",\"" : "\"") + _members[i].first + "\":" + _members[i].second.Dump();
				}
				return out + "}";
			}
		}
		return {};
	}

	// --- helpers ------------------------------------------------------------------------------

	std::string UnitText(Unit a_unit)
	{
		const auto& strings = State::Get().strings;
		switch (a_unit) {
		case Unit::kPoints:
			return strings.Get("$LA_Unit_pts");
		case Unit::kPercent:
			return strings.Get("$LA_Unit_percent");
		case Unit::kFeet:
			return strings.Get("$LA_Unit_ft");
		case Unit::kLevel:
			return strings.Get("$LA_Unit_Levels");
		case Unit::kNone:
			break;
		}
		return {};
	}

	std::string RangesText(const EffectDef& a_def)
	{
		const auto  fmt = State::Get().Formatter();
		std::string out;
		for (const auto range : a_def.AllowedRanges()) {
			if (!out.empty()) {
				out += "/";
			}
			out += fmt.RangeName(range);
		}
		return out;
	}

	std::string SubstituteName(std::string a_text, std::string_view a_name)
	{
		// Morrowind GMSTs use printf's %s; our own strings may use {0}.
		for (const std::string_view token : { "%s"sv, "{0}"sv }) {
			if (const auto pos = a_text.find(token); pos != std::string::npos) {
				a_text.replace(pos, token.size(), a_name);
			}
		}
		return a_text;
	}

	std::string EscapeHtml(std::string_view a_text)
	{
		std::string out;
		out.reserve(a_text.size());
		for (const char c : a_text) {
			switch (c) {
			case '<':
				out += "&lt;";
				break;
			case '>':
				out += "&gt;";
				break;
			case '&':
				out += "&amp;";
				break;
			default:
				out += c;
			}
		}
		return out;
	}

	bool HasVariants(const EffectDef& a_def)
	{
		if (a_def.set == EffectSet::kPassThrough) {
			return a_def.passThroughForm != 0;
		}
		static std::mutex                               mutex;
		static std::unordered_map<std::string, bool>    cache;  // FormMap is fixed after kDataLoaded
		std::scoped_lock                                lock(mutex);
		if (const auto it = cache.find(a_def.id); it != cache.end()) {
			return it->second;
		}
		std::string target;
		if (a_def.target == TargetKind::kAttribute) {
			target = Catalog::AttributeName(0);
		} else if (a_def.target == TargetKind::kSkill) {
			target = Catalog::SkyrimSkillName(0);
		}
		bool found = false;
		const auto& forms = State::Get().forms;
		for (const auto range : a_def.AllowedRanges()) {
			if (forms.Get(Compiler::VariantEditorId(a_def, target, range))) {
				found = true;
				break;
			}
		}
		cache.emplace(a_def.id, found);
		return found;
	}

	namespace
	{
		std::string CostModelName(CostModel a_model)
		{
			const auto& strings = State::Get().strings;
			switch (a_model) {
			case CostModel::kClassic:
				return strings.Get("$LA_UI_CostModel_Classic");
			case CostModel::kEngineAutocalc:
				return strings.Get("$LA_UI_CostModel_EngineAutocalc");
			case CostModel::kSkyrimBalanced:
				break;
			}
			return strings.Get("$LA_UI_CostModel_SkyrimBalanced");
		}

		std::string SchoolIcon(School a_school)
		{
			return ToLower(ToString(a_school));
		}

		double Round2(double a_value) { return std::round(a_value * 100.0) / 100.0; }

		int ArgInt(const std::vector<Val>& a_args, std::size_t a_index, int a_default = 0)
		{
			return a_index < a_args.size() ? static_cast<int>(std::lround(a_args[a_index].AsNum(a_default))) : a_default;
		}

		std::string ArgStr(const std::vector<Val>& a_args, std::size_t a_index)
		{
			return a_index < a_args.size() ? a_args[a_index].AsStr() : std::string{};
		}

		bool ArgBool(const std::vector<Val>& a_args, std::size_t a_index)
		{
			return a_index < a_args.size() && a_args[a_index].AsBool();
		}

		// Names of every spell the player knows (Morrowind's "You already know a spell called").
		bool PlayerKnowsSpellNamed(std::string_view a_name)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return false;
			}
			const auto matches = [&](const RE::SpellItem* a_spell) {
				if (!a_spell) {
					return false;
				}
				const char* name = a_spell->GetName();
				return name && IEquals(name, a_name);
			};
			for (const auto* spell : player->GetActorRuntimeData().addedSpells) {
				if (matches(spell)) {
					return true;
				}
			}
			if (auto* base = player->GetActorBase()) {
				if (const auto* list = base->GetSpellList(); list && list->spells) {
					for (std::uint32_t i = 0; i < list->numSpells; ++i) {
						if (matches(list->spells[i])) {
							return true;
						}
					}
				}
			}
			return false;
		}

		bool PlayerHasFilledSoulGem()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return false;
			}
			auto inventory = player->GetInventory([](RE::TESBoundObject& a_object) { return a_object.IsSoulGem(); });
			for (const auto& [object, data] : inventory) {
				if (data.first <= 0 || !data.second) {
					continue;
				}
				if (data.second->GetSoulLevel() != RE::SOUL_LEVEL::kNone) {
					return true;
				}
			}
			return false;
		}
	}

	// --- MenuController --------------------------------------------------------------------

	MenuController::MenuController(Provider a_provider, RE::ObjectRefHandle a_providerRef, MenuSink* a_sink,
		ControllerOverrides a_overrides) :
		_provider(std::move(a_provider)),
		_providerRef(a_providerRef),
		_sink(a_sink),
		_overrides(std::move(a_overrides))
	{
		{
			const auto& state = State::Get();
			std::shared_lock lock(state.lock);
			_settings = _overrides.settings ? *_overrides.settings : state.settings;
		}
		_showCostMath = _settings.showCostMath;
	}

	Cost::BaseCostOverride MenuController::BaseCost() const
	{
		return State::Get().BaseCostFn();
	}

	void MenuController::Start()
	{
		const auto start = std::chrono::steady_clock::now();
		auto&      state = State::Get();

		std::set<std::string> known;
		if (_overrides.known) {
			known = *_overrides.known;
		} else if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			known = Spellbook::KnownEffects(player);
		}

		// Only effects whose generated records exist can be compiled (a missing optional plugin
		// or a stale formmap must not offer an effect the compiler can't write).
		std::set<std::string> usable;
		std::vector<std::pair<std::string, std::string>> named;  // translated name, id
		const auto fmt = state.Formatter();
		for (const auto& id : known) {
			const auto* def = state.catalog.Find(id);
			if (!def || !HasVariants(*def)) {
				continue;
			}
			usable.insert(id);
			named.emplace_back(ToLower(fmt.EffectName(*def)), id);
		}
		std::sort(named.begin(), named.end());
		_knownSorted.clear();
		_knownSorted.reserve(named.size());
		for (auto& entry : named) {
			_knownSorted.push_back(std::move(entry.second));
		}

		_session = std::make_unique<MenuSession>(state.catalog, _settings, _provider, std::move(usable), state.content.Riders());

		const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		logger::debug("spellmaking menu: session ready ({} known, {} usable) in {:.2f} ms"sv, known.size(), _knownSorted.size(), ms);
	}

	std::uint32_t MenuController::PlayerGold() const
	{
		if (_overrides.gold) {
			return *_overrides.gold;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		return player ? static_cast<std::uint32_t>(std::max(0, player->GetGoldAmount())) : 0;
	}

	PurchaseContext MenuController::BuildContext(bool a_forPurchase) const
	{
		PurchaseContext ctx;
		ctx.gold = PlayerGold();
		if (_settings.haggling && _provider.kind == Provider::Kind::kSpellmaker) {
			const auto handle = _providerRef;
			ctx.haggle = [handle](std::uint32_t a_price) {
				auto ref = handle.get();
				return Services::Haggle(a_price, ref ? ref->As<RE::Actor>() : nullptr);
			};
		}
		if (!a_forPurchase) {
			return ctx;
		}
		{
			const auto&       state = State::Get();
			std::shared_lock lock(state.lock);
			ctx.freePrimarySlots = state.slots.FreePrimaryCount();
			ctx.freeSubSlots = state.slots.FreeSubCount();
		}
		ctx.nameExists = [](std::string_view a_name) { return PlayerKnowsSpellNamed(a_name); };
		ctx.hasFilledSoulGem = PlayerHasFilledSoulGem();
		return ctx;
	}

	std::string MenuController::MessageText(Msg a_msg) const
	{
		auto text = State::Get().MessageText(a_msg);
		if (a_msg == Msg::kNameExists && _session) {
			text = SubstituteName(std::move(text), Trim(_session->Name()));
		}
		return text;
	}

	void MenuController::ShowMessages(const Outcome& a_outcome, bool a_blockingOnly)
	{
		for (const auto msg : a_outcome.messages) {
			if (a_blockingOnly && !IsBlocking(msg)) {
				continue;
			}
			logger::debug("spellmaking menu: message {}"sv, MessageKey(msg));
			if (_sink) {
				_sink->ShowMessage(MessageText(msg));
			}
		}
	}

	void MenuController::PushState()
	{
		if (!_sink || !_session) {
			return;
		}
		const auto start = std::chrono::steady_clock::now();
		auto       state = BuildState();
		const auto built = std::chrono::steady_clock::now();
		_sink->SetState(state);
		const auto end = std::chrono::steady_clock::now();
		logger::debug("spellmaking menu: state built in {:.3f} ms, pushed in {:.3f} ms"sv,
			std::chrono::duration<double, std::milli>(built - start).count(),
			std::chrono::duration<double, std::milli>(end - built).count());
	}

	Val MenuController::BuildKnown() const
	{
		const auto& state = State::Get();
		const auto  fmt = state.Formatter();
		Val         list = Val::Array();
		for (const auto& id : _knownSorted) {
			const auto* def = state.catalog.Find(id);
			if (!def) {
				continue;
			}
			Val entry = Val::Object();
			entry.Set("id", Val::Str(def->id));
			entry.Set("text", Val::Str(fmt.EffectName(*def)));
			entry.Set("school", Val::Num(static_cast<double>(def->school)));
			entry.Set("schoolName", Val::Str(fmt.SchoolName(def->school)));
			entry.Set("baseCost", Val::Num(Round2(def->mwBaseCost)));
			entry.Set("ranges", Val::Str(RangesText(*def)));
			entry.Set("unit", Val::Str(def->hasMagnitude ? UnitText(def->unit) : std::string{}));
			entry.Set("card", Val::Str(def->itemCardKey.empty() ? std::string{} : state.strings.Get(def->itemCardKey)));
			entry.Set("icon", Val::Str(SchoolIcon(def->school)));
			entry.Set("target", Val::Num(static_cast<double>(def->target)));
			list.Push(std::move(entry));
		}
		return list;
	}

	Val MenuController::BuildState() const
	{
		const auto& state = State::Get();
		const auto& catalog = state.catalog;
		const auto  fmt = state.Formatter();
		const auto& session = *_session;
		const auto& effects = session.Effects();

		Val s = Val::Object();
		s.Set("mode", Val::Str(_provider.kind == Provider::Kind::kAltar ? "altar" : "npc"));
		s.Set("providerName", Val::Str(_provider.name));
		s.Set("name", Val::Str(session.Name()));
		s.Set("maxEffects", Val::Num(_settings.maxEffects));
		s.Set("count", Val::Num(static_cast<double>(effects.size())));

		Val rows = Val::Array();
		for (std::size_t i = 0; i < effects.size(); ++i) {
			const auto& effect = effects[i];
			const auto* def = catalog.Find(effect.effectId);
			Val         row = Val::Object();
			row.Set("index", Val::Num(static_cast<double>(i)));
			row.Set("id", Val::Str(effect.effectId));
			row.Set("text", Val::Str(fmt.Line(effect)));
			row.Set("school", Val::Num(def ? static_cast<double>(def->school) : 0.0));
			row.Set("canEdit", Val::Bool(def != nullptr));
			rows.Push(std::move(row));
		}
		s.Set("effects", std::move(rows));

		Val dim = Val::Array();
		for (const auto& id : _knownSorted) {
			if (!session.CanAdd(id)) {
				dim.Push(Val::Str(id));
			}
		}
		s.Set("dim", std::move(dim));

		// Readouts. The magicka cost uses the live-refit base costs (State::BaseCostFn); the price
		// is the one TryCreate will charge (provider fee rules, haggling, load pricing).
		const auto cost = Cost::Compute(catalog, effects, _settings, _settings.costModel, BaseCost());
		const auto ctx = BuildContext(false);
		const auto price = session.PreviewPrice(&ctx);
		s.Set("cost", Val::Num(cost.cost));
		s.Set("costText", Val::Str(std::to_string(cost.cost)));
		s.Set("price", Val::Num(price));
		s.Set("gold", Val::Num(ctx.gold));
		s.Set("canAfford", Val::Bool(price <= ctx.gold));

		int chance = -1;
		if (_settings.castingFailure && !effects.empty()) {
			chance = Casting::CastingChance(RE::PlayerCharacter::GetSingleton(), effects, true);
		}
		s.Set("chance", Val::Num(chance));

		const auto rank = effects.empty() ? Rank::kNovice : Cost::SpellRank(catalog, effects);
		s.Set("rank", Val::Num(static_cast<double>(rank)));
		s.Set("rankName", Val::Str(effects.empty() ? std::string{} : fmt.RankName(rank)));
		s.Set("school", Val::Num(effects.empty() ? -1.0 : static_cast<double>(Cost::CostliestSchool(catalog, effects, _settings))));
		s.Set("modelName", Val::Str(CostModelName(_settings.costModel)));
		s.Set("showCostMath", Val::Bool(_showCostMath));

		Val math = Val::Array();
		const auto targetNote = state.strings.Get("$LA_UI_CostMathTarget");
		for (std::size_t i = 0; i < cost.parts.size() && i < effects.size(); ++i) {
			const auto& part = cost.parts[i];
			auto        text = fmt.Line(effects[i]);
			if (part.targetMult != 1.0) {
				text += "  (" + targetNote + ")";
			}
			Val row = Val::Object();
			row.Set("text", Val::Str(std::move(text)));
			row.Set("share", Val::Num(Round2(part.effectCost)));
			row.Set("runningTotal", Val::Num(Round2(part.runningTotal)));
			math.Push(std::move(row));
		}
		s.Set("costMath", std::move(math));

		// Picker (attribute / skill choice before the editor opens).
		if (const auto& picker = session.Picker()) {
			const auto* def = catalog.Find(picker->effectId);
			Val         p = Val::Object();
			p.Set("effectId", Val::Str(picker->effectId));
			p.Set("title", Val::Str(def ? fmt.EffectName(*def) : picker->effectId));
			Val options = Val::Array();
			for (const auto& [sub, english] : picker->options) {
				Val option = Val::Object();
				option.Set("sub", Val::Num(sub));
				option.Set("text", Val::Str(def ? fmt.TargetName(def->target, sub) : english));
				options.Push(std::move(option));
			}
			p.Set("options", std::move(options));
			s.Set("picker", std::move(p));
		} else {
			s.Set("picker", Val::Null());
		}

		// Effect editor.
		if (const auto& editor = session.Editor(); editor && catalog.Find(editor->effect.effectId)) {
			const auto& draft = editor->effect;
			const auto* def = catalog.Find(draft.effectId);
			const auto  preview = session.PreviewEffects();
			const auto  previewIndex = editor->index < 0 ? preview.size() - 1 : static_cast<std::size_t>(editor->index);
			double      effectCost = 0.0;
			if (!preview.empty()) {
				const auto previewCost = Cost::Compute(catalog, preview, _settings, _settings.costModel, BaseCost());
				if (previewIndex < previewCost.parts.size()) {
					const auto& part = previewCost.parts[previewIndex];
					effectCost = part.effectCost * part.targetMult * _settings.globalCostMult;
				}
			}
			const auto shown = previewIndex < preview.size() ? preview[previewIndex] : draft;

			Val e = Val::Object();
			e.Set("index", Val::Num(editor->index));
			e.Set("id", Val::Str(draft.effectId));
			e.Set("title", Val::Str(fmt.EffectName(*def, draft.sub)));
			e.Set("school", Val::Num(static_cast<double>(def->school)));
			e.Set("schoolName", Val::Str(fmt.SchoolName(def->school)));
			e.Set("icon", Val::Str(SchoolIcon(def->school)));
			e.Set("range", Val::Num(static_cast<double>(draft.range)));
			e.Set("rangeText", Val::Str(fmt.RangeName(draft.range)));
			e.Set("canCycleRange", Val::Bool(def->AllowedRanges().size() > 1));
			e.Set("hasMagnitude", Val::Bool(def->hasMagnitude));
			e.Set("min", Val::Num(shown.minMag));
			e.Set("max", Val::Num(shown.maxMag));
			e.Set("magCap", Val::Num(session.MagnitudeCap()));
			e.Set("hasDuration", Val::Bool(def->hasDuration));
			e.Set("duration", Val::Num(shown.duration));
			e.Set("durCap", Val::Num(session.DurationCap()));
			e.Set("hasArea", Val::Bool(def->ShowsArea(draft.range)));
			e.Set("area", Val::Num(shown.area));
			e.Set("areaCap", Val::Num(session.AreaCap()));
			e.Set("unit", Val::Str(def->hasMagnitude ? UnitText(def->unit) : std::string{}));
			e.Set("lineText", Val::Str(fmt.Line(shown)));
			e.Set("effectCost", Val::Num(effectCost > 0.0 ? std::max(1.0, std::round(effectCost)) : 0.0));
			s.Set("editor", std::move(e));
		} else {
			s.Set("editor", Val::Null());
		}

		Val buttons = Val::Object();
		buttons.Set("createLabel", Val::Str(_provider.kind == Provider::Kind::kSpellmaker ? "$LA_UI_Buy" : "$LA_UI_Create"));
		s.Set("buttons", std::move(buttons));
		return s;
	}

	Val MenuController::BuildLoadList() const
	{
		const auto& state = State::Get();
		const auto  fmt = state.Formatter();
		Val         list = Val::Array();
		// UI callbacks run on the main thread, where every spellbook writer runs too, so the list
		// can't change underneath us. No lock here: Spellbook::List() may take it itself.
		for (const auto* spell : Spellbook::List()) {
			if (!spell) {
				continue;
			}
			std::string text;
			for (const auto& effect : spell->def.effects) {
				if (!text.empty()) {
					text += "; ";
				}
				text += fmt.Line(effect);
			}
			Val entry = Val::Object();
			entry.Set("slot", Val::Num(spell->def.slot));
			entry.Set("name", Val::Str(spell->def.name));
			entry.Set("text", Val::Str(std::move(text)));
			list.Push(std::move(entry));
		}
		return list;
	}

	void MenuController::HandleIntent(std::string_view a_name, const std::vector<Val>& a_args)
	{
		if (!_session) {
			return;
		}
		if (a_name == "LA_PlaySound") {
			if (_sink) {
				_sink->PlaySound(ArgStr(a_args, 0));
			}
			return;
		}
		if (_busy) {
			logger::debug("spellmaking menu: {} ignored while a purchase completes"sv, a_name);
			return;
		}

		auto&   session = *_session;
		Outcome outcome;
		bool    push = true;

		if (a_name == "LA_Ready") {
			if (_sink) {
				const auto start = std::chrono::steady_clock::now();
				_sink->SetKnown(BuildKnown());
				logger::debug("spellmaking menu: LA_SetKnown ({} effects) in {:.2f} ms"sv, _knownSorted.size(),
					std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
			}
		} else if (a_name == "LA_SetName") {
			outcome = session.SetName(ArgStr(a_args, 0));
			push = outcome.changed;
		} else if (a_name == "LA_AddEffect") {
			outcome = session.AddEffect(ArgStr(a_args, 0));
		} else if (a_name == "LA_PickTarget") {
			outcome = session.PickTarget(static_cast<std::int16_t>(ArgInt(a_args, 0, kNoSub)));
		} else if (a_name == "LA_EditEffect") {
			const int index = ArgInt(a_args, 0, -1);
			if (index >= 0) {
				outcome = session.EditEffect(static_cast<std::size_t>(index));
			}
		} else if (a_name == "LA_RemoveEffect") {
			const int index = ArgInt(a_args, 0, -1);
			if (index >= 0) {
				outcome = session.RemoveEffect(static_cast<std::size_t>(index));
			}
		} else if (a_name == "LA_MoveEffect") {
			const int index = ArgInt(a_args, 0, -1);
			if (index >= 0) {
				outcome = session.MoveEffect(static_cast<std::size_t>(index), ArgInt(a_args, 1, 0));
			}
		} else if (a_name == "LA_EditorRange") {
			outcome = session.EditorCycleRange();
		} else if (a_name == "LA_EditorSet") {
			if (const auto field = EditorFieldFromString(ArgStr(a_args, 0))) {
				outcome = session.EditorSet(*field, ArgInt(a_args, 1, 0));
			}
		} else if (a_name == "LA_EditorStep") {
			if (const auto field = EditorFieldFromString(ArgStr(a_args, 0))) {
				outcome = session.EditorStep(*field, ArgInt(a_args, 1, 0), ArgBool(a_args, 2));
			}
		} else if (a_name == "LA_EditorOk") {
			outcome = session.EditorOk();
		} else if (a_name == "LA_EditorCancel") {
			// The SWF backs out of the attribute/skill picker with EditorCancel too (the contract
			// has no picker-cancel call): the picker is the first step of the pending add.
			outcome = session.Picker() ? session.CancelPicker() : session.EditorCancel();
		} else if (a_name == "LA_EditorDelete") {
			outcome = session.EditorDelete();
		} else if (a_name == "LA_Clear") {
			outcome = session.Clear();
		} else if (a_name == "LA_LoadList") {
			if (_sink) {
				_sink->SetLoadList(BuildLoadList());
			}
			push = false;
		} else if (a_name == "LA_Load") {
			const int slot = ArgInt(a_args, 0, -1);
			std::optional<SpellDef> def;
			{
				const auto&       state = State::Get();
				std::shared_lock lock(state.lock);
				if (const auto it = state.spells.find(static_cast<std::uint16_t>(slot)); slot >= 0 && it != state.spells.end()) {
					def = it->second.def;
				}
			}
			if (def) {
				outcome = session.Load(*def);
			}
		} else if (a_name == "LA_Create") {
			Create();
			return;
		} else if (a_name == "LA_ToggleCostMath") {
			_showCostMath = !_showCostMath;
		} else if (a_name == "LA_Exit") {
			if (_sink) {
				_sink->Close();
			}
			return;
		} else if (a_name == "LA_RequestKeyboard") {
			// Proposed SWF extension (gamepad on-screen keyboard). Skyrim PC has no virtual
			// keyboard API; the SWF keeps its own text entry, so nothing to do here.
			logger::debug("spellmaking menu: LA_RequestKeyboard not supported"sv);
			return;
		} else {
			logger::warn("spellmaking menu: unknown call {}"sv, a_name);
			return;
		}

		ShowMessages(outcome);
		if (push) {
			PushState();
		}
	}

	void MenuController::Create()
	{
		auto&      session = *_session;
		const auto ctx = BuildContext(true);
		Outcome    outcome;
		auto       purchase = session.TryCreate(ctx, outcome);
		if (!purchase) {
			ShowMessages(outcome, true);
			PushState();
			return;
		}
		// The magicka cost the readout showed (live-refit base costs) is the one the spell gets.
		if (!State::Get().liveBaseCost.empty()) {
			purchase->def.cost = Cost::Compute(State::Get().catalog, purchase->def.effects, _settings, _settings.costModel, BaseCost()).cost;
		}
		if (_overrides.dryRunPurchase) {
			_lastDryRun = purchase;
			ShowMessages(outcome);
			return;
		}

		_busy = true;
		std::weak_ptr<MenuController> weak = weak_from_this();
		auto                          handle = _providerRef;
		auto                          notices = purchase->notices;
		auto                          name = purchase->def.name;
		RunOnMainThread([weak, handle, purchase = std::move(*purchase), notices = std::move(notices), name = std::move(name)]() mutable {
			auto  ref = handle.get();
			auto* spell = Services::CompletePurchase(purchase, ref.get());
			// Back to the UI side: the menu may have closed meanwhile (weak pointer).
			SKSE::GetTaskInterface()->AddUITask([weak, ok = spell != nullptr, notices = std::move(notices), name = std::move(name)]() mutable {
				if (auto self = weak.lock()) {
					self->OnPurchaseDone(ok, std::move(notices), std::move(name));
				}
			});
		});
	}

	void MenuController::OnPurchaseDone(bool a_ok, std::vector<Msg> a_notices, std::string a_name)
	{
		_busy = false;
		if (!a_ok) {
			logger::warn("spellmaking menu: purchase of '{}' failed"sv, a_name);
			if (_sink) {
				_sink->ShowMessage(State::Get().MessageText(Msg::kSpellbookFull));
			}
			PushState();
			return;
		}
		// Notices (a known spell with the same name) don't block; with the window closing they
		// go to the HUD instead of the menu's message box.
		for (const auto msg : a_notices) {
			RE::SendHUDMessage::ShowHUDMessage(SubstituteName(State::Get().MessageText(msg), a_name).c_str());
		}
		if (_settings.closeAfterCreate) {
			if (_sink) {
				_sink->Close();
			}
			return;
		}
		_session->Reset();
		PushState();
	}
}
