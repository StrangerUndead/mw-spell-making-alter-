#pragma once

// Plugin-wide state: data loaded at kDataLoaded plus the per-save spellbook. Everything that
// mutates game records runs on the main thread (SKSE task interface); readers on other threads
// take the shared lock.

namespace LA
{
	// Resolves generated records (data/generated/formmap.json) and vanilla references.
	class FormMap
	{
	public:
		bool Load(const std::filesystem::path& a_file, std::vector<std::string>& a_errors);

		// Generated record by EditorID, e.g. "LA_FireDamage_Target", "LA_Slot_042".
		RE::TESForm* Get(std::string_view a_editorId) const;
		template <class T>
		T* Get(std::string_view a_editorId) const
		{
			auto* form = Get(a_editorId);
			return form ? form->As<T>() : nullptr;
		}
		// "Skyrim.esm|0x012FD0" style reference.
		static RE::TESForm* Resolve(const FormRef& a_ref);
		template <class T>
		static T* Resolve(const FormRef& a_ref)
		{
			auto* form = Resolve(a_ref);
			return form ? form->As<T>() : nullptr;
		}

		std::size_t Size() const { return _ids.size(); }
		// Reverse lookup for our own records (e.g. MGEF -> "LA_FireDamage_Target").
		std::string_view EditorIdOf(RE::FormID a_formId) const;

	private:
		struct Entry
		{
			std::string   plugin;
			std::uint32_t localId{ 0 };
		};
		std::unordered_map<std::string, Entry>        _ids;
		mutable std::unordered_map<RE::FormID, std::string> _reverse;
		mutable std::once_flag                        _reverseBuilt;
		// Resolved once in Load() (kDataLoaded): EditorID -> form. Read-only afterwards.
		std::unordered_map<std::string, RE::TESForm*> _forms;
	};

	// One custom spell as it lives in the game: its definition and the records it occupies.
	struct CustomSpell
	{
		SpellDef                      def;
		RE::SpellItem*                primary{ nullptr };
		std::vector<RE::SpellItem*>   subs;       // linked sub-spells (same order as plan.spells[1..])
		CompilePlan                   plan;
	};

	class State
	{
	public:
		static State& Get();

		// --- Data (read-only after kDataLoaded) ---------------------------------------------
		Catalog     catalog;
		Content     content;
		Discovery   discovery;
		StringTable strings;
		FormMap     forms;
		Settings    settings;
		std::filesystem::path dataDir;       // Data/SKSE/Plugins/LostArt
		bool        dataReady{ false };

		// Live refit of Skyrim-balanced base costs (effect id -> B_sk), filled at kDataLoaded.
		std::unordered_map<std::string, double> liveBaseCost;
		Cost::BaseCostOverride BaseCostFn() const;

		// --- Per-save state (guarded by lock) -------------------------------------------------
		mutable std::shared_mutex lock;
		SlotPool                  slots;
		std::map<std::uint16_t, CustomSpell> spells;  // by primary slot
		std::vector<Mark>         marks;
		std::vector<LedgerEntry>  ledger;
		std::vector<ConditionEntry> conditions;
		VersionInfo               version;

		// Slot records resolved once at kDataLoaded: LA_Slot_000.. / LA_Sub_000..
		std::vector<RE::SpellItem*> primarySlots;
		std::vector<RE::SpellItem*> subSlots;

		// Helpers ---------------------------------------------------------------------------
		EffectFormatter Formatter() const { return EffectFormatter(catalog, strings, settings.effectNames == 1); }
		std::string     Text(std::string_view a_key) const { return strings.Get(a_key); }
		std::string     MessageText(Msg a_msg) const { return strings.Get("$LA_Msg_" + std::string(MessageKey(a_msg))); }

		// Custom spell owning this form (primary or sub), if any. Takes the shared lock itself; the
		// pointer stays valid only while no main-thread writer erases the spell, so readers off the
		// main thread that keep using it take `lock` and call FindBySpellLocked instead.
		const CustomSpell* FindBySpell(const RE::SpellItem* a_spell) const;
		// Same, for callers that already hold `lock` (shared or unique) or run on the main thread.
		const CustomSpell* FindBySpellLocked(const RE::SpellItem* a_spell) const;
		bool               IsCustomSpell(const RE::SpellItem* a_spell) const { return FindBySpell(a_spell) != nullptr; }
		std::optional<std::uint16_t> SlotOf(const RE::SpellItem* a_spell) const;

		void ReloadSettings();

		// --- Foundation additions (Core/*.cpp) ------------------------------------------------
		// Effects whose LA_ variants (or rider MGEFs) are missing from the load order: hidden from
		// the menu, compiled as the inert placeholder. Filled at kDataLoaded, read-only after.
		std::unordered_set<std::string> hiddenEffects;
		bool EffectUsable(std::string_view a_effectId) const;

		RE::EffectSetting* blankEffect{ nullptr };       // LA_BlankEffect (LostArt_Slots.esp)
		RE::TESGlobal*     altarsEnabled{ nullptr };     // LA_AltarsEnabled
		RE::TESGlobal*     spellmakersEnabled{ nullptr };// LA_SpellmakersEnabled
		// Half-cost perks (ranks.json), [school][rank]; nullptr when unresolved.
		std::array<std::array<RE::BGSPerk*, 5>, kSchoolCount> castingPerks{};
		RE::BGSPerk* CastingPerk(School a_school, Rank a_rank) const;

		// Pass-through effects discovered at runtime are appended to `catalog` here (main thread;
		// once per id, never replaced; Catalog stores a deque, so Find() pointers stay valid).
		void AddPassThrough(EffectDef a_def);

		// Quest-protected spells (Spellbook::CanDelete -> kQuestSpell): built-in list plus
		// Data/SKSE/Plugins/LostArt/content/protected-spells.json when present. Full FormIDs.
		std::unordered_set<RE::FormID> protectedSpells;

		// Summary of the kDataLoaded pass (for `la slots`, tests and the log).
		std::vector<std::string> loadWarnings;

		// Settings side effects: log level and the LA_AltarsEnabled / LA_SpellmakersEnabled globals.
		void ApplySettings();

		// Slot record -> index (kSubFlag set for sub slots). Built at kDataLoaded, read-only after.
		static constexpr std::uint16_t kSubFlag = 0x8000;
		std::unordered_map<const RE::SpellItem*, std::uint16_t> slotIndex;
		// Sub slot -> owning primary slot (kNoSlot when unused). Per-save, guarded by lock.
		std::vector<std::uint16_t> subOwner;

		static bool IsMainThread();
		static void MarkMainThread();  // called once from SKSEPluginLoad (the main thread)

	private:
		State() = default;
	};

	// kDataLoaded: reads every data file, resolves records, verifies variants, refits base costs.
	// Returns false when the plugin cannot work at all (no slots); the rest degrades with warnings.
	bool LoadData();

	// Runs a_task on the main thread (SKSE task interface); immediately if already there.
	void RunOnMainThread(std::function<void()> a_task);

	// Settings file locations (MCM Helper).
	std::filesystem::path SettingsDefaultsPath();  // Data/MCM/Config/LostArt/settings.ini
	std::filesystem::path SettingsUserPath();      // Data/MCM/Settings/LostArt.ini
}
