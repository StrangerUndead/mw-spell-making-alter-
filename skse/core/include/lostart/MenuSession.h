#pragma once

#include "lostart/Catalog.h"
#include "lostart/Compiler.h"
#include "lostart/CostEngine.h"
#include "lostart/Settings.h"
#include "lostart/Types.h"

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace LA
{
	// Messages the menu can raise. The first eight are Morrowind's GMSTs.
	enum class Msg : std::uint8_t
	{
		kMaxEffects,     // sNotifyMessage28   You can only add eight effects to a spell.
		kDuplicate,      // sOnetypeEffectMessage
		kNoEffects,      // sNotifyMessage30
		kNoName,         // sNotifyMessage10
		kZeroCost,       // sEnchantmentMenu8
		kNoGold,         // sNotifyMessage18
		kSpellbookFull,  // beyond Morrowind
		kNameExists,     // notice only
		kTooComplex,
		kNeedSoulGem
	};

	// Translation key suffix ("$LA_Msg_" + key) for a message.
	std::string_view MessageKey(Msg a_msg);
	// Whether the message stops the action (every Morrowind message does; kNameExists doesn't).
	bool IsBlocking(Msg a_msg);

	enum class EditorField : std::uint8_t
	{
		kMin,
		kMax,
		kDuration,
		kArea
	};
	std::optional<EditorField> EditorFieldFromString(std::string_view a_text);

	struct Provider
	{
		enum class Kind : std::uint8_t
		{
			kAltar,
			kSpellmaker
		};
		Kind          kind{ Kind::kAltar };
		std::string   name;
		std::uint32_t formId{ 0 };
		bool          freeService{ false };  // Arch-Mage at the College altar
		double        priceMult{ 1.0 };      // per-NPC modifier (setting)
	};

	struct Outcome
	{
		std::vector<Msg> messages;
		bool             changed{ false };
		bool             ok() const
		{
			for (auto m : messages) {
				if (IsBlocking(m)) {
					return false;
				}
			}
			return true;
		}
	};

	struct EditorState
	{
		int         index{ -1 };  // -1: a new effect
		SpellEffect effect;
		SpellEffect original;     // for Cancel on an existing row
	};

	struct PickerState
	{
		std::string                                       effectId;
		std::vector<std::pair<std::int16_t, std::string>> options;  // sub, display text (filled by caller)
	};

	struct PurchaseContext
	{
		std::uint32_t                           gold{ 0 };
		std::size_t                             freePrimarySlots{ 0 };
		std::size_t                             freeSubSlots{ 0 };
		bool                                    hasFilledSoulGem{ false };
		std::function<bool(std::string_view)>   nameExists;       // known spell with this name
		std::function<std::uint32_t(std::uint32_t)> haggle;       // Skyrim buy-price adjustment
		const SpellDef*                         replacing{ nullptr };  // Load + replace-original
	};

	struct Purchase
	{
		SpellDef    def;         // slot left unassigned (kNoSlot) unless replacing
		CompilePlan plan;
		std::uint32_t price{ 0 };
		bool        consumeSoulGem{ false };
		std::vector<Msg> notices;
	};

	// The spellmaking menu's rules (OUTLINE "Spellmaking menu" and parity rows 4-18).
	// The SWF only renders what this reports.
	class MenuSession
	{
	public:
		MenuSession(const Catalog& a_catalog, const Settings& a_settings, Provider a_provider,
			std::set<std::string> a_knownEffects, RiderLookup a_riders = {});

		// Intents -----------------------------------------------------------------------------
		Outcome SetName(std::string_view a_name);
		Outcome AddEffect(std::string_view a_effectId);
		Outcome PickTarget(std::int16_t a_sub);
		Outcome CancelPicker();
		Outcome EditEffect(std::size_t a_index);
		Outcome RemoveEffect(std::size_t a_index);
		Outcome MoveEffect(std::size_t a_index, int a_delta);
		Outcome EditorCycleRange();
		Outcome EditorSet(EditorField a_field, int a_value);
		Outcome EditorStep(EditorField a_field, int a_steps, bool a_big);
		Outcome EditorOk();
		Outcome EditorCancel();
		Outcome EditorDelete();
		Outcome Clear();
		Outcome Load(const SpellDef& a_def);

		// Runs Morrowind's buy checks in order, then the slot and complexity checks. On success
		// returns the purchase; the caller takes the gold, compiles, and calls Reset().
		std::optional<Purchase> TryCreate(const PurchaseContext& a_ctx, Outcome& a_outcome) const;
		void Reset();

		// Queries -----------------------------------------------------------------------------
		const std::string&              Name() const { return _name; }
		const std::vector<SpellEffect>& Effects() const { return _effects; }
		const std::optional<EditorState>& Editor() const { return _editor; }
		const std::optional<PickerState>& Picker() const { return _picker; }
		const Provider&                 GetProvider() const { return _provider; }
		const std::set<std::string>&    Known() const { return _known; }
		std::optional<std::uint16_t>    LoadedFromSlot() const { return _loadedFrom; }

		// The effects as they would be bought (the editor's draft applied).
		std::vector<SpellEffect> PreviewEffects() const;
		CostResult               PreviewCost() const;
		std::uint32_t            PreviewPrice(const PurchaseContext* a_ctx = nullptr) const;

		// Whether an effect can be added right now (dimmed rows in Effects Known).
		bool CanAdd(std::string_view a_effectId) const;

		// Magnitude/duration/area caps for the editor.
		int MagnitudeCap() const { return _settings.magnitudeCap; }
		int DurationCap() const { return _settings.durationCap; }
		int AreaCap() const { return _settings.areaCap; }

		// Starting range for a new effect (parity row 13).
		Range StartingRange(const EffectDef& a_def) const;

		const Catalog&  GetCatalog() const { return _catalog; }
		const Settings& GetSettings() const { return _settings; }

	private:
		bool     HasDuplicate(std::string_view a_id, std::int16_t a_sub, int a_ignoreIndex) const;
		void     OpenEditorForNew(const EffectDef& a_def, std::int16_t a_sub);
		void     Normalize(SpellEffect& a_effect) const;
		std::size_t MaxEffects() const { return static_cast<std::size_t>(_settings.maxEffects); }

		const Catalog&             _catalog;
		const Settings&            _settings;
		Provider                   _provider;
		std::set<std::string>      _known;
		RiderLookup                _riders;
		std::string                _name;
		std::vector<SpellEffect>   _effects;
		std::optional<EditorState> _editor;
		std::optional<PickerState> _picker;
		std::optional<std::uint16_t> _loadedFrom;
		std::uint32_t              _loadedPricePaid{ 0 };
	};
}
