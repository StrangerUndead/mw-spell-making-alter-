#pragma once

#include "Core/State.h"

// The spellmaking menu's controller, separated from the Scaleform host so the in-game parity
// tests drive exactly the code path the SWF uses (docs/dev/CONTRACTS.md section 7):
//
//   SWF --GameDelegate.call(name, args)--> SpellmakingMenu (IMenu/FxDelegate)
//       --> MenuController::HandleIntent(name, args) --> core MenuSession (all rules)
//       --> MenuSink::SetState / ShowMessage / ... --> Invoke on _root.Menu_mc
//
// Payloads are built as a small value tree (Val) and converted to GFxValue by the host, so
// tests can read the same readouts the SWF receives. Owner: UI/*.cpp.
namespace LA::UI
{
	// A JSON-like value mirroring the GFx types the protocol uses.
	class Val
	{
	public:
		enum class Type : std::uint8_t
		{
			kUndefined,
			kNull,
			kBool,
			kNumber,
			kString,
			kArray,
			kObject
		};

		Val() = default;
		static Val Null() { return Val(Type::kNull); }
		static Val Bool(bool a_value);
		static Val Num(double a_value);
		static Val Str(std::string a_value);
		static Val Array() { return Val(Type::kArray); }
		static Val Object() { return Val(Type::kObject); }

		Type Kind() const { return _type; }
		bool IsNull() const { return _type == Type::kNull || _type == Type::kUndefined; }

		// Object members keep insertion order (debug dumps read like the contract).
		Val&       Set(std::string a_key, Val a_value);
		const Val* Get(std::string_view a_key) const;
		void       Push(Val a_value);

		bool                                      AsBool(bool a_default = false) const;
		double                                    AsNum(double a_default = 0.0) const;
		std::string                               AsStr() const;
		const std::vector<Val>&                   Items() const { return _items; }
		const std::vector<std::pair<std::string, Val>>& Members() const { return _members; }

		std::string Dump() const;  // compact JSON, for logs and test details

	private:
		explicit Val(Type a_type) :
			_type(a_type)
		{}

		Type                                     _type{ Type::kUndefined };
		bool                                     _bool{ false };
		double                                   _num{ 0.0 };
		std::string                              _str;
		std::vector<Val>                         _items;
		std::vector<std::pair<std::string, Val>> _members;
	};

	// DLL -> SWF calls (CONTRACTS section 7, "Invoke on _root.Menu_mc").
	class MenuSink
	{
	public:
		virtual ~MenuSink() = default;
		virtual void SetKnown(const Val& a_known) = 0;
		virtual void SetState(const Val& a_state) = 0;
		virtual void SetLoadList(const Val& a_list) = 0;
		virtual void ShowMessage(const std::string& a_text) = 0;
		virtual void Close() = 0;
		virtual void PlaySound(const std::string& a_soundKey) = 0;
	};

	// Test-only overrides; the live menu never sets them.
	struct ControllerOverrides
	{
		std::optional<std::set<std::string>> known;           // replaces the player's known effects
		std::optional<std::uint32_t>         gold;            // replaces the player's gold
		std::optional<Settings>              settings;        // replaces the settings snapshot
		bool                                 dryRunPurchase{ false };  // Create stops after TryCreate
	};

	class MenuController : public std::enable_shared_from_this<MenuController>
	{
	public:
		MenuController(Provider a_provider, RE::ObjectRefHandle a_providerRef, MenuSink* a_sink,
			ControllerOverrides a_overrides = {});

		// Builds the known-effect list and the session. Must be called once before intents.
		void Start();

		// Every SWF -> DLL call lands here (same names and argument order as the protocol).
		void HandleIntent(std::string_view a_name, const std::vector<Val>& a_args);

		// The host is going away: stop talking to it.
		void Detach() { _sink = nullptr; }

		// Queries (tests, host).
		const MenuSession*  Session() const { return _session.get(); }
		const Provider&     GetProvider() const { return _provider; }
		RE::ObjectRefHandle ProviderRef() const { return _providerRef; }
		const Settings&     GetSettings() const { return _settings; }
		bool                Busy() const { return _busy; }
		std::optional<Purchase> LastDryRun() const { return _lastDryRun; }

		Val BuildKnown() const;
		Val BuildState() const;
		Val BuildLoadList() const;

		// Known effect ids that have generated variants (the list the menu shows).
		const std::vector<std::string>& KnownSorted() const { return _knownSorted; }

		// Called on the UI thread once CompletePurchase finished on the main thread.
		void OnPurchaseDone(bool a_ok, std::vector<Msg> a_notices, std::string a_name);

	private:
		void            PushState();
		void            ShowMessages(const Outcome& a_outcome, bool a_blockingOnly = false);
		std::string     MessageText(Msg a_msg) const;
		void            Create();
		PurchaseContext BuildContext(bool a_forPurchase) const;
		std::uint32_t   PlayerGold() const;
		Cost::BaseCostOverride BaseCost() const;

		Provider                     _provider;
		RE::ObjectRefHandle          _providerRef;
		MenuSink*                    _sink{ nullptr };
		ControllerOverrides          _overrides;
		Settings                     _settings;  // snapshot: MenuSession keeps a reference to it
		std::unique_ptr<MenuSession> _session;
		std::vector<std::string>     _knownSorted;  // by translated name
		bool                         _showCostMath{ false };
		bool                         _busy{ false };
		std::optional<Purchase>      _lastDryRun;
	};

	// Utilities shared by the UI files.
	std::string UnitText(Unit a_unit);
	std::string RangesText(const EffectDef& a_def);
	std::string SubstituteName(std::string a_text, std::string_view a_name);  // Morrowind "%s"
	std::string EscapeHtml(std::string_view a_text);

	// Effects whose generated variants (or pass-through MGEF) exist in the loaded plugins.
	bool HasVariants(const EffectDef& a_def);

	// Parity suite registration (UI/ParityTests.cpp); idempotent.
	void RegisterParityTests();

	// The live controller of the open menu, if any (tests, console).
	std::shared_ptr<MenuController> ActiveController();
}
