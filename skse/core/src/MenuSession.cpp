#include "lostart/MenuSession.h"

#include "lostart/Util.h"

#include <algorithm>

namespace LA
{
	std::string_view MessageKey(Msg a_msg)
	{
		switch (a_msg) {
		case Msg::kMaxEffects:
			return "sNotifyMessage28";
		case Msg::kDuplicate:
			return "sOnetypeEffectMessage";
		case Msg::kNoEffects:
			return "sNotifyMessage30";
		case Msg::kNoName:
			return "sNotifyMessage10";
		case Msg::kZeroCost:
			return "sEnchantmentMenu8";
		case Msg::kNoGold:
			return "sNotifyMessage18";
		case Msg::kSpellbookFull:
			return "SpellbookFull";
		case Msg::kNameExists:
			return "NameExists";
		case Msg::kTooComplex:
			return "TooComplex";
		case Msg::kNeedSoulGem:
			return "NeedSoulGem";
		}
		return "";
	}

	bool IsBlocking(Msg a_msg) { return a_msg != Msg::kNameExists; }

	std::optional<EditorField> EditorFieldFromString(std::string_view a_text)
	{
		if (IEquals(a_text, "min")) {
			return EditorField::kMin;
		}
		if (IEquals(a_text, "max")) {
			return EditorField::kMax;
		}
		if (IEquals(a_text, "duration")) {
			return EditorField::kDuration;
		}
		if (IEquals(a_text, "area")) {
			return EditorField::kArea;
		}
		return std::nullopt;
	}

	MenuSession::MenuSession(const Catalog& a_catalog, const Settings& a_settings, Provider a_provider,
		std::set<std::string> a_knownEffects, RiderLookup a_riders) :
		_catalog(a_catalog),
		_settings(a_settings),
		_provider(std::move(a_provider)),
		_known(std::move(a_knownEffects)),
		_riders(std::move(a_riders))
	{}

	Range MenuSession::StartingRange(const EffectDef& a_def) const
	{
		const auto allowed = a_def.AllowedRanges();
		if (_settings.startingRange == 0) {
			// OpenMW rule: Touch for three-range effects, Target for Touch/Target ones.
			if (allowed.size() == 3) {
				return Range::kTouch;
			}
			if (a_def.Allows(Range::kTouch) && a_def.Allows(Range::kTarget) && !a_def.Allows(Range::kSelf)) {
				return Range::kTarget;
			}
		}
		return allowed.empty() ? Range::kSelf : allowed.front();
	}

	bool MenuSession::HasDuplicate(std::string_view a_id, std::int16_t a_sub, int a_ignoreIndex) const
	{
		for (std::size_t i = 0; i < _effects.size(); ++i) {
			if (static_cast<int>(i) != a_ignoreIndex && _effects[i].effectId == a_id && _effects[i].sub == a_sub) {
				return true;
			}
		}
		return false;
	}

	bool MenuSession::CanAdd(std::string_view a_effectId) const
	{
		if (_effects.size() >= MaxEffects()) {
			return false;
		}
		const auto* def = _catalog.Find(a_effectId);
		if (!def) {
			return false;
		}
		if (def->target != TargetKind::kNone) {
			return true;  // another attribute or skill may still be free
		}
		return !HasDuplicate(a_effectId, kNoSub, -1);
	}

	void MenuSession::Normalize(SpellEffect& a_effect) const
	{
		const auto* def = _catalog.Find(a_effect.effectId);
		if (!def) {
			return;
		}
		const auto magCap = static_cast<std::uint16_t>(_settings.magnitudeCap);
		a_effect.minMag = std::clamp<std::uint16_t>(a_effect.minMag, 1, magCap);
		a_effect.maxMag = std::clamp<std::uint16_t>(a_effect.maxMag, a_effect.minMag, magCap);
		a_effect.duration = def->hasDuration ? std::clamp<std::uint16_t>(a_effect.duration, 1, static_cast<std::uint16_t>(_settings.durationCap)) : 1;
		a_effect.area = def->ShowsArea(a_effect.range) ? std::min<std::uint16_t>(a_effect.area, static_cast<std::uint16_t>(_settings.areaCap)) : 0;
		if (!def->Allows(a_effect.range)) {
			a_effect.range = StartingRange(*def);
		}
	}

	void MenuSession::OpenEditorForNew(const EffectDef& a_def, std::int16_t a_sub)
	{
		// Parity row 13: a new effect starts at 1 to 1 pts, 1 sec, 0 ft.
		EditorState editor;
		editor.index = -1;
		editor.effect.effectId = a_def.id;
		editor.effect.sub = a_sub;
		editor.effect.range = StartingRange(a_def);
		editor.effect.minMag = 1;
		editor.effect.maxMag = 1;
		editor.effect.duration = 1;
		editor.effect.area = 0;
		editor.original = editor.effect;
		_editor = editor;
	}

	Outcome MenuSession::SetName(std::string_view a_name)
	{
		Outcome outcome;
		auto    name = Utf8Truncate(a_name, kMaxNameLength);
		if (name != _name) {
			_name = std::move(name);
			outcome.changed = true;
		}
		return outcome;
	}

	Outcome MenuSession::AddEffect(std::string_view a_effectId)
	{
		Outcome outcome;
		if (_editor || _picker) {
			return outcome;
		}
		const auto* def = _catalog.Find(a_effectId);
		if (!def || !_known.contains(std::string(a_effectId))) {
			return outcome;
		}
		// Parity row 5, then row 6: the limit is checked before duplicates.
		if (_effects.size() >= MaxEffects()) {
			outcome.messages.push_back(Msg::kMaxEffects);
			return outcome;
		}
		if (def->target != TargetKind::kNone) {
			PickerState picker;
			picker.effectId = def->id;
			if (def->target == TargetKind::kAttribute) {
				for (std::int16_t i = 0; i < static_cast<std::int16_t>(kAttributeCount); ++i) {
					picker.options.emplace_back(i, std::string(Catalog::AttributeName(i)));
				}
			} else if (_settings.skillPicker == 1 && !_catalog.MwSkills().empty()) {
				for (const auto& skill : _catalog.MwSkills()) {
					picker.options.emplace_back(static_cast<std::int16_t>(kMwSkillBase + skill.index), skill.name);
				}
			} else {
				for (std::int16_t i = 0; i < static_cast<std::int16_t>(kSkyrimSkillCount); ++i) {
					picker.options.emplace_back(i, std::string(Catalog::SkyrimSkillName(i)));
				}
			}
			_picker = std::move(picker);
			outcome.changed = true;
			return outcome;
		}
		if (HasDuplicate(def->id, kNoSub, -1)) {
			outcome.messages.push_back(Msg::kDuplicate);
			return outcome;
		}
		OpenEditorForNew(*def, kNoSub);
		outcome.changed = true;
		return outcome;
	}

	Outcome MenuSession::PickTarget(std::int16_t a_sub)
	{
		Outcome outcome;
		if (!_picker) {
			return outcome;
		}
		const auto valid = std::any_of(_picker->options.begin(), _picker->options.end(), [&](const auto& o) { return o.first == a_sub; });
		if (!valid) {
			return outcome;
		}
		const auto* def = _catalog.Find(_picker->effectId);
		_picker.reset();
		outcome.changed = true;
		if (!def) {
			return outcome;
		}
		if (HasDuplicate(def->id, a_sub, -1)) {
			outcome.messages.push_back(Msg::kDuplicate);
			return outcome;
		}
		OpenEditorForNew(*def, a_sub);
		return outcome;
	}

	Outcome MenuSession::CancelPicker()
	{
		Outcome outcome;
		if (_picker) {
			_picker.reset();
			outcome.changed = true;
		}
		return outcome;
	}

	Outcome MenuSession::EditEffect(std::size_t a_index)
	{
		Outcome outcome;
		if (_editor || _picker || a_index >= _effects.size()) {
			return outcome;
		}
		EditorState editor;
		editor.index = static_cast<int>(a_index);
		editor.effect = _effects[a_index];
		editor.original = editor.effect;
		_editor = editor;
		outcome.changed = true;
		return outcome;
	}

	Outcome MenuSession::RemoveEffect(std::size_t a_index)
	{
		Outcome outcome;
		if (_editor || a_index >= _effects.size()) {
			return outcome;
		}
		_effects.erase(_effects.begin() + static_cast<std::ptrdiff_t>(a_index));
		outcome.changed = true;
		return outcome;
	}

	Outcome MenuSession::MoveEffect(std::size_t a_index, int a_delta)
	{
		Outcome outcome;
		if (_editor || a_index >= _effects.size() || a_delta == 0) {
			return outcome;
		}
		const auto target = static_cast<std::ptrdiff_t>(a_index) + (a_delta > 0 ? 1 : -1);
		if (target < 0 || target >= static_cast<std::ptrdiff_t>(_effects.size())) {
			return outcome;
		}
		std::swap(_effects[a_index], _effects[static_cast<std::size_t>(target)]);
		outcome.changed = true;
		return outcome;
	}

	Outcome MenuSession::EditorCycleRange()
	{
		Outcome outcome;
		if (!_editor) {
			return outcome;
		}
		const auto* def = _catalog.Find(_editor->effect.effectId);
		if (!def) {
			return outcome;
		}
		// Parity row 8: Self -> Touch -> Target, skipping ranges the effect can't use.
		auto& effect = _editor->effect;
		for (int step = 1; step <= 3; ++step) {
			const auto next = static_cast<Range>((static_cast<int>(effect.range) + step) % 3);
			if (def->Allows(next)) {
				if (next != effect.range) {
					effect.range = next;
					// Parity row 11: area resets to 0 on Self.
					if (!def->ShowsArea(next)) {
						effect.area = 0;
					}
					outcome.changed = true;
				}
				break;
			}
		}
		return outcome;
	}

	Outcome MenuSession::EditorSet(EditorField a_field, int a_value)
	{
		Outcome outcome;
		if (!_editor) {
			return outcome;
		}
		const auto* def = _catalog.Find(_editor->effect.effectId);
		if (!def) {
			return outcome;
		}
		auto       effect = _editor->effect;
		const int  magCap = _settings.magnitudeCap;
		switch (a_field) {
		case EditorField::kMin:
			if (!def->hasMagnitude) {
				return outcome;
			}
			// Parity row 9: raising min pulls max up.
			effect.minMag = static_cast<std::uint16_t>(std::clamp(a_value, 1, magCap));
			effect.maxMag = std::max(effect.maxMag, effect.minMag);
			break;
		case EditorField::kMax:
			if (!def->hasMagnitude) {
				return outcome;
			}
			// Parity row 9: max can't drop below min.
			effect.maxMag = static_cast<std::uint16_t>(std::clamp(a_value, static_cast<int>(effect.minMag), magCap));
			break;
		case EditorField::kDuration:
			if (!def->hasDuration) {
				return outcome;
			}
			effect.duration = static_cast<std::uint16_t>(std::clamp(a_value, 1, _settings.durationCap));
			break;
		case EditorField::kArea:
			if (!def->ShowsArea(effect.range)) {
				return outcome;
			}
			effect.area = static_cast<std::uint16_t>(std::clamp(a_value, 0, _settings.areaCap));
			break;
		}
		if (!(effect == _editor->effect)) {
			_editor->effect = effect;
			outcome.changed = true;
		}
		return outcome;
	}

	Outcome MenuSession::EditorStep(EditorField a_field, int a_steps, bool a_big)
	{
		if (!_editor) {
			return {};
		}
		// Parity row 12: track clicks step magnitude 10, duration 20, area 5; arrows step 1.
		int step = 1;
		int current = 0;
		const auto& effect = _editor->effect;
		switch (a_field) {
		case EditorField::kMin:
			step = a_big ? 10 : 1;
			current = effect.minMag;
			break;
		case EditorField::kMax:
			step = a_big ? 10 : 1;
			current = effect.maxMag;
			break;
		case EditorField::kDuration:
			step = a_big ? 20 : 1;
			current = effect.duration;
			break;
		case EditorField::kArea:
			step = a_big ? 5 : 1;
			current = effect.area;
			break;
		}
		return EditorSet(a_field, current + a_steps * step);
	}

	Outcome MenuSession::EditorOk()
	{
		Outcome outcome;
		if (!_editor) {
			return outcome;
		}
		auto effect = _editor->effect;
		Normalize(effect);
		if (_editor->index < 0) {
			if (_effects.size() >= MaxEffects()) {
				outcome.messages.push_back(Msg::kMaxEffects);
			} else if (HasDuplicate(effect.effectId, effect.sub, -1)) {
				outcome.messages.push_back(Msg::kDuplicate);
			} else {
				_effects.push_back(effect);
			}
		} else if (static_cast<std::size_t>(_editor->index) < _effects.size()) {
			_effects[static_cast<std::size_t>(_editor->index)] = effect;
		}
		_editor.reset();
		outcome.changed = true;
		return outcome;
	}

	Outcome MenuSession::EditorCancel()
	{
		Outcome outcome;
		if (_editor) {
			_editor.reset();  // a new effect is dropped; an edited one keeps its old values
			outcome.changed = true;
		}
		return outcome;
	}

	Outcome MenuSession::EditorDelete()
	{
		Outcome outcome;
		if (!_editor) {
			return outcome;
		}
		const int index = _editor->index;
		_editor.reset();
		if (index >= 0 && static_cast<std::size_t>(index) < _effects.size()) {
			_effects.erase(_effects.begin() + index);
		}
		outcome.changed = true;
		return outcome;
	}

	Outcome MenuSession::Clear()
	{
		Outcome outcome;
		outcome.changed = !_effects.empty() || !_name.empty() || _editor || _picker || _loadedFrom;
		Reset();
		return outcome;
	}

	void MenuSession::Reset()
	{
		_effects.clear();
		_name.clear();
		_editor.reset();
		_picker.reset();
		_loadedFrom.reset();
		_loadedPricePaid = 0;
	}

	Outcome MenuSession::Load(const SpellDef& a_def)
	{
		Outcome outcome;
		if (_editor || _picker) {
			return outcome;
		}
		_effects.clear();
		for (auto effect : a_def.effects) {
			if (_catalog.Find(effect.effectId) && _effects.size() < MaxEffects()) {
				Normalize(effect);
				_effects.push_back(effect);
			}
		}
		_name = Utf8Truncate(a_def.name, kMaxNameLength);
		_loadedFrom = a_def.slot;
		_loadedPricePaid = a_def.pricePaid;
		outcome.changed = true;
		return outcome;
	}

	std::vector<SpellEffect> MenuSession::PreviewEffects() const
	{
		auto effects = _effects;
		if (_editor) {
			auto draft = _editor->effect;
			Normalize(draft);
			if (_editor->index < 0) {
				effects.push_back(draft);
			} else if (static_cast<std::size_t>(_editor->index) < effects.size()) {
				effects[static_cast<std::size_t>(_editor->index)] = draft;
			}
		}
		return effects;
	}

	CostResult MenuSession::PreviewCost() const
	{
		return Cost::Compute(_catalog, _effects, _settings);
	}

	std::uint32_t MenuSession::PreviewPrice(const PurchaseContext* a_ctx) const
	{
		const auto cost = PreviewCost();
		if (_effects.empty()) {
			return 0;
		}
		std::uint32_t price = cost.price;
		if (_provider.freeService) {
			return 0;
		}
		if (_provider.kind == Provider::Kind::kAltar) {
			switch (_settings.altarFee) {
			case AltarFee::kFull:
				break;
			case AltarFee::kHalf:
				price = std::max<std::uint32_t>(1, price / 2);
				break;
			case AltarFee::kFree:
			case AltarFee::kFilledSoulGem:
				return 0;
			}
		} else if (_settings.perNPCPrices) {
			price = static_cast<std::uint32_t>(std::max<std::int64_t>(1, FloorTol(price * _provider.priceMult)));
		}
		if (_settings.haggling && a_ctx && a_ctx->haggle) {
			price = a_ctx->haggle(price);
		}
		if (_loadedFrom && _settings.loadPricing == 1) {
			price = price > _loadedPricePaid ? price - _loadedPricePaid : 0;
		}
		return price;
	}

	std::optional<Purchase> MenuSession::TryCreate(const PurchaseContext& a_ctx, Outcome& a_outcome) const
	{
		// Parity row 18: no effects, no name, zero cost, not enough gold - in that order.
		if (_effects.empty()) {
			a_outcome.messages.push_back(Msg::kNoEffects);
			return std::nullopt;
		}
		const auto name = Trim(_name);
		if (name.empty()) {
			a_outcome.messages.push_back(Msg::kNoName);
			return std::nullopt;
		}
		const auto cost = PreviewCost();
		if (cost.cost == 0) {
			a_outcome.messages.push_back(Msg::kZeroCost);
			return std::nullopt;
		}
		const auto price = PreviewPrice(&a_ctx);
		const bool soulGemFee = _provider.kind == Provider::Kind::kAltar && _settings.altarFee == AltarFee::kFilledSoulGem && !_provider.freeService;
		if (soulGemFee && !a_ctx.hasFilledSoulGem) {
			a_outcome.messages.push_back(Msg::kNeedSoulGem);
			return std::nullopt;
		}
		if (price > a_ctx.gold) {
			a_outcome.messages.push_back(Msg::kNoGold);
			return std::nullopt;
		}

		const auto plan = Compiler::Plan(_catalog, _effects, _settings, _riders);
		const bool replacing = a_ctx.replacing != nullptr;
		const std::size_t freeSubs = a_ctx.freeSubSlots + (replacing ? a_ctx.replacing->subSlots.size() : 0);
		if ((!replacing && a_ctx.freePrimarySlots == 0) || plan.SubSlotsNeeded() > freeSubs) {
			a_outcome.messages.push_back(Msg::kSpellbookFull);
			return std::nullopt;
		}
		if (plan.tooComplex) {
			a_outcome.messages.push_back(Msg::kTooComplex);
			return std::nullopt;
		}

		Purchase purchase;
		if (a_ctx.nameExists && a_ctx.nameExists(name) && !(replacing && a_ctx.replacing->name == name)) {
			purchase.notices.push_back(Msg::kNameExists);
			a_outcome.messages.push_back(Msg::kNameExists);
		}
		purchase.def.slot = replacing ? a_ctx.replacing->slot : kNoSlot;
		purchase.def.name = name;
		purchase.def.effects = _effects;
		purchase.def.costModel = _settings.costModel;
		purchase.def.cost = cost.cost;
		purchase.def.pricePaid = price + (replacing && _settings.loadPricing == 1 ? a_ctx.replacing->pricePaid : 0);
		purchase.def.provider = _provider.formId;
		purchase.def.flags = replacing ? SpellFlags::kReplaced : 0;
		purchase.plan = plan;
		purchase.price = price;
		purchase.consumeSoulGem = soulGemFee;
		return purchase;
	}
}
