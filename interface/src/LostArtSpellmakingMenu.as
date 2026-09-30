/*
 * Lost Art of Spellmaking - spellmaking menu (root clip _root.Menu_mc).
 *
 * Contract: docs/dev/CONTRACTS.md section 7. The DLL (core MenuSession) owns every rule, limit,
 * cost and message; this movie renders the state snapshots it receives and sends intents back
 * through gfx.io.GameDelegate.call. Nothing here decides whether an action is legal.
 *
 *   DLL -> SWF : LA_SetKnown(arr) LA_SetState(obj) LA_SetLoadList(arr) LA_ShowMessage(text)
 *                LA_Close()        [+ proposed: LA_KeyboardResult(purpose, text, accepted)]
 *   SWF -> DLL : LA_Ready LA_SetName LA_AddEffect LA_PickTarget LA_EditEffect LA_RemoveEffect
 *                LA_MoveEffect LA_EditorRange LA_EditorSet LA_EditorStep LA_EditorOk
 *                LA_EditorCancel LA_EditorDelete LA_Clear LA_LoadList LA_Load LA_Create
 *                LA_ToggleCostMath LA_Exit LA_PlaySound  [+ proposed: LA_RequestKeyboard]
 *
 * Layout (design space 1080 high, see lostart.util.Layout):
 *   name row / Effects Known (tabs, search, list) | Spell Effects (n/8, list) + item card /
 *   readout bar + Create|Buy, Load, Clear, Exit. Editor, picker, load list and message box
 *   are modal layers on top.
 */
import gfx.io.GameDelegate;
import lostart.Sounds;
import lostart.Theme;
import lostart.components.Button;
import lostart.components.CostMathPanel;
import lostart.components.EffectEditor;
import lostart.components.ItemCard;
import lostart.components.KeyGlyph;
import lostart.components.ListPopup;
import lostart.components.MessageBox;
import lostart.components.ReadoutBar;
import lostart.components.SkyIcon;
import lostart.components.TabBar;
import lostart.components.TextBox;
import lostart.components.VirtualList;
import lostart.input.InputDetails;
import lostart.input.InputRouter;
import lostart.input.KeyMap;
import lostart.model.EffectFilter;
import lostart.model.StateUtil;
import lostart.util.Draw;
import lostart.util.Layout;
import lostart.util.Text;
import lostart.util.Translator;
import lostart.util.Tween;

class LostArtSpellmakingMenu extends MovieClip
{
	public static var NAME_MAX_CHARS: Number = 40;

	/* ---- layers ---- */
	private var _content: MovieClip;
	private var _overlay: MovieClip;
	private var _fadeMc: MovieClip;

	/* ---- header ---- */
	private var _titleTf: TextField;
	private var _headerLine: MovieClip;

	/* ---- name row ---- */
	private var _nameLabelTf: TextField;
	private var _nameBox: TextBox;
	private var _nameGlyph: KeyGlyph;
	private var _nameHintTf: TextField;

	/* ---- Effects Known ---- */
	private var _knownPanel: MovieClip;
	private var _knownTitleTf: TextField;
	private var _knownCostTf: TextField;
	private var _searchBox: TextBox;
	private var _searchGlyph: KeyGlyph;
	private var _tabs: TabBar;
	private var _knownList: VirtualList;

	/* ---- Spell Effects ---- */
	private var _effectsPanel: MovieClip;
	private var _effectsTitleTf: TextField;
	private var _counterTf: TextField;
	private var _effectsList: VirtualList;
	private var _reorderGlyphA: KeyGlyph;
	private var _reorderGlyphB: KeyGlyph;
	private var _reorderTf: TextField;

	/* ---- card / cost math / bar ---- */
	private var _card: ItemCard;
	private var _costMath: CostMathPanel;
	private var _barPanel: MovieClip;
	private var _readout: ReadoutBar;
	private var _createBtn: Button;
	private var _loadBtn: Button;
	private var _clearBtn: Button;
	private var _exitBtn: Button;

	/* ---- modals ---- */
	private var _editor: EffectEditor;
	private var _picker: ListPopup;
	private var _loadPopup: ListPopup;
	private var _msg: MessageBox;

	/* ---- state ---- */
	private var _input: InputRouter;
	private var _frame: Object;
	private var _known: Array;
	private var _knownById: Object;
	private var _filtered: Array;
	private var _dim: Object;
	private var _state: Object;
	private var _pane: String = "known";
	private var _device: String = "kbm";
	private var _pendingSel: Number;
	private var _closing: Boolean = false;
	private var _lastSound: Object;
	private var _layoutRight: Object;
	private var _layoutCard: Object;
	private var _layoutLeftLow: Object;

	public var devMode: Boolean = false;

	public function LostArtSpellmakingMenu()
	{
		super();
	}

	/* Called once by LostArtMain after the clip is bound to this class. */
	public function init(): Void
	{
		_known = [];
		_knownById = {};
		_filtered = [];
		_dim = {};
		_lastSound = {};

		_content = createEmptyMovieClip("content", 1);
		_overlay = _content.createEmptyMovieClip("overlay", 1);
		_fadeMc = createEmptyMovieClip("fade", 2);
		buildNameRow();
		Text.setCaps(_titleTf, "$LA_UI_AltarTitle");
		buildKnownPane();
		buildEffectsPane();
		buildCardAndBar();
		buildModals();

		var self: LostArtSpellmakingMenu = this;
		_input = new InputRouter(this);
		_input.onDeviceChange = function(d: String): Void { self.setDevice(d); };
		var stageListener: Object = {};
		stageListener.onResize = function(): Void { self.relayout(); };
		Stage.addListener(stageListener);
		this.onUnload = function(): Void { self.releaseTextInput(); };

		relayout();
		setDevice("kbm");
		applyFilter(undefined);

		_alpha = 0;
		Tween.run(_fadeMc, 0, 100, 150, function(v: Number): Void { self._alpha = v; }, undefined);

		send("LA_Ready", []);
	}

	/* ================================================================================
	 * DLL -> SWF
	 * ================================================================================ */

	public function LA_SetKnown(a_known: Array): Void
	{
		_known = a_known == undefined ? [] : a_known;
		_knownById = {};
		for (var i: Number = 0; i < _known.length; i++)
			_knownById[String(_known[i].id)] = _known[i];
		_tabs.setCounts(EffectFilter.countBySchool(_known));
		applyFilter(currentKnownId());
		updateCard();
	}

	public function LA_SetState(a_state: Object): Void
	{
		if (a_state == undefined)
			return;
		var prev: Object = _state;
		_state = a_state;

		// menu title: the altar, or the spellmaker's name (vanilla crafting menus title the station)
		var providerName: String = StateUtil.str(a_state.providerName);
		if (a_state.mode == "npc")
			Text.setCaps(_titleTf, providerName.length > 0 ? providerName : "$LA_UI_SpellmakerTitle");
		else
			Text.setCaps(_titleTf, "$LA_UI_AltarTitle");

		// name (never overwrite while the player types)
		_nameBox.setText(StateUtil.str(a_state.name));

		// dimmed known rows
		var dim: Object = StateUtil.toSet(a_state.dim);
		if (!StateUtil.sameSet(dim, _dim)) {
			_dim = dim;
			_knownList.refresh();
		}

		// spell effects
		var effects: Array = a_state.effects == undefined ? [] : a_state.effects;
		if (prev == undefined || !StateUtil.sameEffects(prev.effects, effects)) {
			if (prev != undefined && prev.effects != undefined) {
				if (effects.length > prev.effects.length)
					playSound(Sounds.ADD);
				else if (effects.length < prev.effects.length)
					playSound(Sounds.REMOVE);
			}
			var keepSel: Number = _effectsList.selectedIndex;
			_effectsList.setData(effects);
			if (_pendingSel != undefined) {
				_effectsList.select(_pendingSel, false, true);
				_pendingSel = undefined;
			} else if (prev != undefined && prev.effects != undefined && effects.length > prev.effects.length) {
				_effectsList.select(effects.length - 1, false, true);   // show the new row
			} else {
				_effectsList.select(keepSel, false, true);
			}
		}
		_counterTf.text = String(StateUtil.num(a_state.count, effects.length)) + "/" + String(StateUtil.num(a_state.maxEffects, 8));
		Text.setColor(_counterTf, effects.length >= StateUtil.num(a_state.maxEffects, 8) ? Theme.TEXT : Theme.TEXT_SOFT);

		// readouts + buttons
		_readout.setState(a_state, spellSchoolColor(a_state), prev != undefined);
		var createLabel: String = a_state.buttons != undefined && a_state.buttons.createLabel != undefined ?
			String(a_state.buttons.createLabel) : "$LA_UI_Create";
		_createBtn.setLabel(createLabel);
		layoutBar();

		// picker
		if (a_state.picker != undefined && a_state.picker != null) {
			var wasOpen: Boolean = _picker.isOpen;
			var accent: Number = Theme.schoolColor(knownSchool(String(a_state.picker.effectId)));
			_picker.open(StateUtil.str(a_state.picker.title), a_state.picker.options, "", accent);
			if (!wasOpen)
				playSound(Sounds.OK);
		} else if (_picker.isOpen) {
			_picker.close();
		}

		// editor
		var ed: Object = a_state.editor;
		if (ed != undefined && ed != null) {
			if (ed.schoolName == undefined)
				ed.schoolName = knownSchoolName(String(ed.id));
			var edKnown: Object = _knownById[String(ed.id)];
			if (ed.icon == undefined && edKnown != undefined) {
				ed.icon = edKnown.icon;
				ed.iconColor = edKnown.iconColor;
			}
			var opening: Boolean = !_editor.isOpen;
			_editor.setData(ed);
			if (opening)
				playSound(Sounds.OK);
		} else if (_editor.isOpen) {
			_editor.setData(null);
		}

		updatePaneLook();
		updateCard();
		updateOverlays();
	}

	public function LA_SetLoadList(a_list: Array): Void
	{
		_loadPopup.open("$LA_UI_LoadTitle", a_list == undefined ? [] : a_list, "$LA_UI_LoadEmpty", Theme.ACCENT);
		playSound(Sounds.OK);
	}

	public function LA_ShowMessage(a_text: String): Void
	{
		_msg.show(a_text);
		playSound(Sounds.ERROR);
	}

	public function LA_Close(): Void
	{
		if (_closing)
			return;
		_closing = true;
		releaseTextInput();
		var self: LostArtSpellmakingMenu = this;
		Tween.run(_fadeMc, _alpha, 0, 150, function(v: Number): Void { self._alpha = v; }, undefined);
	}

	/*
	 * PROPOSED contract addition (docs/dev/MENU.md): result of the on-screen keyboard the DLL
	 * opened for LA_RequestKeyboard. purpose: "name" | "search".
	 */
	public function LA_KeyboardResult(a_purpose: String, a_text: String, a_accepted: Boolean): Void
	{
		var t: String = a_text == undefined ? "" : String(a_text);
		if (a_purpose == "search") {
			_searchBox.end(false);
			if (a_accepted)
				_searchBox.setText(t);
			applyFilter(currentKnownId());
		} else {
			_nameBox.end(false);
			if (a_accepted) {
				if (t.length > NAME_MAX_CHARS)
					t = t.substr(0, NAME_MAX_CHARS);
				_nameBox.setText(t);
				send("LA_SetName", [t]);
			}
		}
	}

	/* ================================================================================
	 * construction
	 * ================================================================================ */

	private function buildNameRow(): Void
	{
		var self: LostArtSpellmakingMenu = this;
		// Menu title, top left (vanilla crafting menus: "ENCHANTING", "SMITHING").
		_titleTf = Text.spacing(Text.create(_content, "menuTitle", 8, 0, 0, 600, 44, Theme.FS_MENU_TITLE, Theme.TEXT, Theme.FONT_MEDIUM, "left"), 3);
		_headerLine = _content.createEmptyMovieClip("headerLine", 9);
		_nameLabelTf = Text.label(_content, "nameLabel", 10, 300, "left");
		Text.setCaps(_nameLabelTf, "$LA_UI_Name");
		_nameBox = new TextBox(_content, "nameBox", 11, Theme.FS_HEADER + 4, NAME_MAX_CHARS, false);
		_nameBox.setPlaceholder("$LA_UI_NameHint");
		_nameBox.onChange = function(t: String): Void { self.send("LA_SetName", [t]); };
		_nameBox.onStart = function(): Void { self.onTextStart("name"); };
		_nameBox.onEnd = function(t: String, accepted: Boolean): Void { self.onNameEnd(t, accepted); };
		_nameGlyph = new KeyGlyph(_content, "nameGlyph", 12, 26);
		_nameHintTf = Text.create(_content, "nameHint", 13, 0, 0, 300, 30, Theme.FS_HINT, Theme.TEXT_HINT, Theme.FONT_MEDIUM, "left");
	}

	private function buildKnownPane(): Void
	{
		var self: LostArtSpellmakingMenu = this;
		_knownPanel = _content.createEmptyMovieClip("knownPanel", 20);
		var p: MovieClip = _knownPanel;
		p.createEmptyMovieClip("bg", 1);
		// SkyUI column header: grey capitals over a fading rule ("EFFECTS KNOWN ... BASE COST").
		_knownTitleTf = Text.label(p, "title", 2, 300, "left");
		Text.setCaps(_knownTitleTf, "$LA_UI_EffectsKnown");
		_knownCostTf = Text.label(p, "costHead", 7, 200, "right");
		Text.setCaps(_knownCostTf, "$LA_UI_BaseCost");
		_searchBox = new TextBox(p, "search", 3, Theme.FS_SMALL, 40, true);
		_searchBox.setPlaceholder("$LA_UI_Search");
		_searchBox.onChange = function(t: String): Void { self.applyFilter(self.currentKnownId()); };
		_searchBox.onStart = function(): Void { self.onTextStart("search"); };
		_searchBox.onEnd = function(t: String, accepted: Boolean): Void { self.onSearchEnd(t, accepted); };
		_searchGlyph = new KeyGlyph(p, "searchGlyph", 4, 24);

		var labels: Array = ["$LA_UI_TabAll", "$LA_UI_TabAlteration", "$LA_UI_TabConjuration",
			"$LA_UI_TabDestruction", "$LA_UI_TabIllusion", "$LA_UI_TabRestoration"];
		_tabs = new TabBar(p, "tabs", 5, labels, undefined);
		_tabs.onChange = function(i: Number): Void {
			self.playSound(Sounds.TAB);
			self.applyFilter(self.currentKnownId());
		};

		_knownList = new VirtualList(p, "list", 6, Theme.ROW_H);
		_knownList.createRow = function(mc: MovieClip, w: Number, h: Number): Object { return self.createKnownRow(mc, w, h); };
		_knownList.renderRow = function(row: Object, e: Object, i: Number, sel: Boolean, foc: Boolean): Void { self.renderKnownRow(row, e, i, sel, foc); };
		_knownList.onSelectionChange = function(i: Number, byMouse: Boolean): Void { self.onListSelection("known", byMouse); };
		_knownList.onItemPress = function(i: Number): Void { self.setPane("known", true); self.addSelected(); };
	}

	private function buildEffectsPane(): Void
	{
		var self: LostArtSpellmakingMenu = this;
		_effectsPanel = _content.createEmptyMovieClip("effectsPanel", 30);
		var p: MovieClip = _effectsPanel;
		p.createEmptyMovieClip("bg", 1);
		_effectsTitleTf = Text.label(p, "title", 2, 300, "left");
		Text.setCaps(_effectsTitleTf, "$LA_UI_SpellEffects");
		_counterTf = Text.spacing(Text.create(p, "counter", 3, 0, 0, 120, 30, Theme.FS_BODY, Theme.TEXT, Theme.FONT_MEDIUM, "right"), Theme.LETTER_SPACING);
		_effectsList = new VirtualList(p, "list", 4, Theme.ROW_H_EFFECT);
		_effectsList.hoverSelects = true;
		_effectsList.createRow = function(mc: MovieClip, w: Number, h: Number): Object { return self.createEffectRow(mc, w, h); };
		_effectsList.renderRow = function(row: Object, e: Object, i: Number, sel: Boolean, foc: Boolean): Void { self.renderEffectRow(row, e, i, sel, foc); };
		_effectsList.onSelectionChange = function(i: Number, byMouse: Boolean): Void { self.onListSelection("effects", byMouse); };
		_effectsList.onItemPress = function(i: Number): Void { self.setPane("effects", true); self.editSelected(); };
		_effectsList.emptyText = "$LA_UI_NoEffects";
		_reorderGlyphA = new KeyGlyph(p, "rgA", 5, 22);
		_reorderGlyphB = new KeyGlyph(p, "rgB", 6, 22);
		_reorderTf = Text.create(p, "reorder", 7, 0, 0, 400, 26, Theme.FS_HINT, Theme.TEXT_HINT, Theme.FONT_MEDIUM, "left");
	}

	private function buildCardAndBar(): Void
	{
		var self: LostArtSpellmakingMenu = this;
		_card = new ItemCard(_content, "card", 40);
		_costMath = new CostMathPanel(_content, "costMath", 41);

		_barPanel = _content.createEmptyMovieClip("bar", 50);
		_readout = new ReadoutBar(_barPanel, "readout", 2);
		_createBtn = new Button(_barPanel, "create", 3, 48);
		_loadBtn = new Button(_barPanel, "load", 4, 48);
		_clearBtn = new Button(_barPanel, "clear", 5, 48);
		_exitBtn = new Button(_barPanel, "exit", 6, 48);
		_createBtn.setLabel("$LA_UI_Create");
		_loadBtn.setLabel("$LA_UI_Load");
		_clearBtn.setLabel("$LA_UI_Clear");
		_exitBtn.setLabel("$LA_UI_Exit");
		_createBtn.onPress = function(): Void { self.doAction(KeyMap.CREATE); };
		_loadBtn.onPress = function(): Void { self.doAction(KeyMap.LOAD); };
		_clearBtn.onPress = function(): Void { self.doAction(KeyMap.CLEAR); };
		_exitBtn.onPress = function(): Void { self.doAction(KeyMap.CANCEL); };
	}

	private function buildModals(): Void
	{
		var self: LostArtSpellmakingMenu = this;
		_editor = new EffectEditor(_content, "editor", 60);
		_editor.onStep = function(f: String, steps: Number, big: Boolean): Void {
			self.playSound(Sounds.STEP);
			self.send("LA_EditorStep", [f, steps, big]);
		};
		_editor.onSet = function(f: String, v: Number): Void { self.send("LA_EditorSet", [f, v]); };
		_editor.onRange = function(): Void {
			self.playSound(Sounds.TAB);
			self.send("LA_EditorRange", []);
		};
		_editor.onOk = function(): Void { self.send("LA_EditorOk", []); };
		_editor.onCancel = function(): Void {
			self.playSound(Sounds.CANCEL);
			self.send("LA_EditorCancel", []);
		};
		_editor.onDelete = function(): Void { self.send("LA_EditorDelete", []); };
		_editor.onFocusMove = function(): Void { self.playSound(Sounds.FOCUS); };

		_picker = new ListPopup(_content, "picker", 70, false);
		_picker.onPick = function(e: Object, i: Number): Void { self.send("LA_PickTarget", [Number(e.sub)]); };
		// The contract has no picker-cancel call; the picker is part of the pending add, so
		// backing out of it cancels that edit (the DLL clears state.picker).
		_picker.onCancel = function(): Void {
			self.playSound(Sounds.CANCEL);
			self.send("LA_EditorCancel", []);
		};
		_picker.onFocusMove = function(): Void { self.playSound(Sounds.FOCUS); };

		_loadPopup = new ListPopup(_content, "loadList", 80, true);
		_loadPopup.onPick = function(e: Object, i: Number): Void {
			self._loadPopup.close();
			self.send("LA_Load", [Number(e.slot)]);
		};
		_loadPopup.onCancel = function(): Void {
			self.playSound(Sounds.CANCEL);
			self._loadPopup.close();
		};
		_loadPopup.onFocusMove = function(): Void { self.playSound(Sounds.FOCUS); };

		_msg = new MessageBox(_content, "message", 90);
		_msg.onClosed = function(): Void { self.playSound(Sounds.OK); };
	}

	/* ================================================================================
	 * layout
	 * ================================================================================ */

	public function relayout(): Void
	{
		var f: Object = Layout.frame();
		_frame = f;
		_content._x = f.x;
		_content._y = f.y;
		_content._xscale = _content._yscale = f.scale * 100;

		var X: Number = f.contentX;
		var Y: Number = f.contentY;
		var W: Number = f.contentW;
		var H: Number = f.contentH;
		var gap: Number = Theme.GAP;
		var barH: Number = 96;
		var headH: Number = 64;
		var leftW: Number = Math.round(W * 0.37);
		var rightX: Number = X + leftW + gap * 3;
		var rightW: Number = X + W - rightX;

		// Backdrop, as the vanilla crafting menus: the scene is dimmed, the list side sits on a
		// black gradient that fades out towards the middle, and the bottom bar on a black band.
		_overlay.clear();
		Draw.rect(_overlay, 0, 0, f.width, f.height, Theme.OVERLAY, Theme.OVERLAY_ALPHA);
		// One full-width gradient (no internal edges): opaque behind the known list, a lighter
		// dip between the columns, darker again behind the spell and the card.
		var r1: Number = Math.round(255 * (X + leftW + gap) / f.width);
		var r2: Number = Math.round(255 * (rightX - gap) / f.width);
		Draw.gradient(_overlay, 0, 0, f.width, f.height, 0x000000,
			[Theme.BACKDROP_ALPHA, Theme.BACKDROP_ALPHA, 45, 50], [0, Math.max(0, r1 - 30), r2, 255], false);
		Draw.gradient(_overlay, 0, f.height - barH - 70, f.width, barH + 70, 0x000000, [0, 85, 92], [0, 120, 255], true);
		Draw.divider(_overlay, 0, f.height - barH - 8, f.width, 26);

		// header: menu title over a fading rule
		_titleTf._x = X;
		_titleTf._y = Y;
		_titleTf._width = leftW;
		_headerLine.clear();
		Draw.divider(_headerLine, X - 20, Y + 48, leftW + 40, 40);

		// name (top of the right column): label, then the name on an underline
		_nameLabelTf._x = rightX;
		_nameLabelTf._y = Y + 2;
		var hintW: Number = 260;
		_nameBox.clip._x = rightX;
		_nameBox.clip._y = Y + 20;
		_nameBox.setSize(rightW - hintW - gap, 44);
		_nameGlyph.clip._x = rightX + rightW - hintW + 6;
		_nameGlyph.clip._y = Y + 20 + Math.round((44 - 26) / 2);
		_nameHintTf._y = Y + 20 + Math.round((44 - 26) / 2) - 1;
		_nameHintTf._width = hintW - 40;

		var mainY: Number = Y + headH + gap;
		var mainH: Number = H - headH - barH - 2 * gap + 8;

		layoutKnown(X, mainY, leftW, mainH);

		var cardH: Number = Math.max(190, Math.round(mainH * 0.34));
		var effH: Number = mainH - cardH - gap;
		layoutEffects(rightX, mainY, rightW, effH);
		var cardY: Number = mainY + effH + gap;
		_card.clip._x = rightX;
		_card.clip._y = cardY;
		_card.setSize(rightW, cardH);
		_layoutRight = {x: rightX, y: mainY, w: rightW, h: mainH};
		_layoutCard = {x: rightX, y: cardY, w: rightW, h: cardH};
		_layoutLeftLow = {x: X, y: mainY + mainH - cardH, w: leftW, h: cardH};

		_barPanel._x = X;
		_barPanel._y = f.height - barH;
		_barPanel["w"] = W;
		_barPanel["h"] = barH - 8;
		layoutBar();

		_editor.setBounds(rightX, mainY, rightW, mainH);
		var popW: Number = Math.min(760, Math.round(W * 0.5));
		var popH: Number = Math.min(820, mainH);
		_picker.setBounds(f.width, f.height, popW, popH);
		_loadPopup.setBounds(f.width, f.height, Math.min(900, Math.round(W * 0.6)), popH);
		_msg.setFrame(f.width, f.height);

		updatePaneLook();
		updateOverlays();
		updateHints();
	}

	/*
	 * The editor covers the whole right column, so the Spell Effects pane and the card are
	 * hidden under it (no bleed-through). Cost math normally replaces the item card; while the
	 * editor is open it moves over the lower part of Effects Known so it stays visible.
	 */
	private function updateOverlays(): Void
	{
		if (_layoutCard == undefined)
			return;
		var editing: Boolean = _editor.isOpen;
		_effectsPanel._visible = !editing;
		var g: Object = editing ? _layoutLeftLow : _layoutCard;
		_costMath.clip._x = g.x;
		_costMath.clip._y = g.y;
		_costMath.setSize(g.w, g.h);
		if (_state != undefined)
			_costMath.setData(_state.showCostMath == true, _state.costMath, StateUtil.str(_state.modelName), Translator.tr(StateUtil.str(_state.costText)));
		_card.clip._visible = !editing && !_costMath.clip._visible;
	}

	private function layoutKnown(a_x: Number, a_y: Number, a_w: Number, a_h: Number): Void
	{
		var p: MovieClip = _knownPanel;
		p._x = a_x;
		p._y = a_y;
		p["w"] = a_w;
		p["h"] = a_h;
		// school icon bar with the active school's name under its rule
		var tabsH: Number = Theme.TAB_ICON_SIZE + 44;
		_tabs.clip._x = 0;
		_tabs.clip._y = 0;
		_tabs.setSize(a_w, tabsH);
		// search box on the caption line, right-aligned (SkyUI puts it over the list)
		var sw: Number = Math.round(a_w * 0.5);
		_searchBox.clip._x = a_w - sw;
		_searchBox.clip._y = Theme.TAB_ICON_SIZE + 16;
		_searchBox.setSize(sw - 34, 30);
		_searchGlyph.clip._x = a_w - 28;
		_searchGlyph.clip._y = Theme.TAB_ICON_SIZE + 19;
		// column header
		var headY: Number = tabsH + 12;
		_knownTitleTf._x = 0;
		_knownTitleTf._y = headY;
		_knownTitleTf._width = a_w * 0.6;
		_knownCostTf._x = a_w - 214;
		_knownCostTf._y = headY;
		_knownCostTf._width = 200;
		var bg: MovieClip = p.bg;
		bg.clear();
		Draw.divider(bg, -10, headY + 26, a_w + 20, Theme.BORDER_ALPHA + 10);
		var listY: Number = headY + 32;
		_knownList.clip._x = 0;
		_knownList.clip._y = listY;
		_knownList.setSize(a_w, a_h - listY);
	}

	private function layoutEffects(a_x: Number, a_y: Number, a_w: Number, a_h: Number): Void
	{
		var p: MovieClip = _effectsPanel;
		p._x = a_x;
		p._y = a_y;
		p["w"] = a_w;
		p["h"] = a_h;
		_effectsTitleTf._x = 0;
		_effectsTitleTf._y = 2;
		_effectsTitleTf._width = a_w * 0.6;
		_counterTf._x = a_w - 134;
		_counterTf._y = -2;
		var bg: MovieClip = p.bg;
		bg.clear();
		Draw.divider(bg, -10, 28, a_w + 20, Theme.BORDER_ALPHA + 10);
		var hintH: Number = 30;
		_effectsList.clip._x = 0;
		_effectsList.clip._y = 34;
		_effectsList.setSize(a_w, a_h - 34 - hintH - 4);
		var hy: Number = a_h - hintH + 2;
		_reorderGlyphA.clip._x = 0;
		_reorderGlyphA.clip._y = hy;
		_reorderGlyphB.clip._y = hy;
		_reorderTf._y = hy - 1;
	}

	private function layoutBar(): Void
	{
		// SkyUI bottom bar: button hints on the left, the readouts on the right.
		var p: MovieClip = _barPanel;
		var w: Number = p["w"];
		var h: Number = p["h"];
		if (w == undefined)
			return;
		var btns: Array = [_createBtn, _loadBtn, _clearBtn, _exitBtn];
		var bx: Number = 0;
		for (var i: Number = 0; i < btns.length; i++) {
			var b: Button = btns[i];
			b.setMinWidth(0);
			b.clip._x = bx;
			b.clip._y = Math.round((h - b.height) / 2);
			bx += b.width + 14;
		}
		_readout.clip._x = bx + 20;
		_readout.clip._y = 0;
		_readout.setSize(w - bx - 20, h);
	}

	/* ================================================================================
	 * rows
	 * ================================================================================ */

	private function createKnownRow(a_mc: MovieClip, a_w: Number, a_h: Number): Object
	{
		var bg: MovieClip = a_mc.createEmptyMovieClip("bg", 1);
		var icon: SkyIcon = new SkyIcon(a_mc, "icon", 2, Theme.ICONS_ITEM, Theme.ICON_SIZE);
		var tf: TextField = Text.create(a_mc, "name", 3, 44, 0, a_w - 44 - 90, a_h, Theme.FS_BODY, Theme.TEXT, Theme.FONT_MEDIUM, "left");
		var cost: TextField = Text.create(a_mc, "cost", 4, a_w - 90, 0, 76, a_h, Theme.FS_BODY, Theme.TEXT, Theme.FONT_MEDIUM, "right");
		Text.spacing(tf, 0.6);
		var row: Object = {bg: bg, icon: icon, tf: tf, cost: cost};
		row.resize = function(w: Number, h: Number): Void {
			tf._width = w - 44 - 90;
			cost._x = w - 90;
		};
		return row;
	}

	private function renderKnownRow(a_row: Object, a_e: Object, a_i: Number, a_sel: Boolean, a_focused: Boolean): Void
	{
		var bg: MovieClip = a_row.bg;
		var tf: TextField = a_row.tf;
		var cost: TextField = a_row.cost;
		var h: Number = a_row.height;
		var dimmed: Boolean = _dim[String(a_e.id)] == true;
		bg.clear();
		if (a_sel)
			Draw.selectBar(bg, -6, 1, a_row.width + 12, h - 2, a_focused);
		var icon: SkyIcon = a_row.icon;
		icon.clip._x = 10;
		icon.clip._y = Math.round((h - Theme.ICON_SIZE) / 2);
		icon.show(iconLabel(a_e), iconColor(a_e));
		icon.clip._alpha = dimmed ? 30 : 100;
		Text.setFit(tf, String(a_e.text));
		tf._y = Math.round((h - tf.textHeight) / 2) - 3;
		cost.text = a_e.baseCost == undefined ? "" : ItemCard.formatNumber(Number(a_e.baseCost));
		cost._y = tf._y;
		var c: Number = dimmed ? Theme.TEXT_DIM : (a_sel && a_focused ? Theme.TEXT : Theme.TEXT_SOFT);
		Text.setColor(tf, c);
		Text.setColor(cost, dimmed ? Theme.TEXT_DIM : Theme.TEXT_HINT);
	}

	private function createEffectRow(a_mc: MovieClip, a_w: Number, a_h: Number): Object
	{
		var self: LostArtSpellmakingMenu = this;
		var bg: MovieClip = a_mc.createEmptyMovieClip("bg", 1);
		var icon: SkyIcon = new SkyIcon(a_mc, "icon", 2, Theme.ICONS_ITEM, Theme.ICON_SIZE + 4);
		var tf: TextField = Text.multiline(Text.create(a_mc, "line", 3, 48, 2, a_w - 48 - 110, a_h - 4, Theme.FS_BODY, Theme.TEXT, Theme.FONT_REGULAR, "left"));
		var tools: MovieClip = a_mc.createEmptyMovieClip("tools", 4);
		var up: MovieClip = tools.createEmptyMovieClip("up", 1);
		var down: MovieClip = tools.createEmptyMovieClip("down", 2);
		var del: MovieClip = tools.createEmptyMovieClip("del", 3);
		var row: Object = {bg: bg, icon: icon, tf: tf, tools: tools, up: up, down: down, del: del};
		up.onRelease = function(): Void { self.moveRow(row.index, -1); };
		down.onRelease = function(): Void { self.moveRow(row.index, 1); };
		del.onRelease = function(): Void { self.removeRow(row.index); };
		up.useHandCursor = down.useHandCursor = del.useHandCursor = false;
		row.resize = function(w: Number, h: Number): Void {
			tf._width = w - 48 - 110;
			tools._x = w - 104;
		};
		row.resize(a_w, a_h);
		return row;
	}

	private function renderEffectRow(a_row: Object, a_e: Object, a_i: Number, a_sel: Boolean, a_focused: Boolean): Void
	{
		var bg: MovieClip = a_row.bg;
		var tf: TextField = a_row.tf;
		var h: Number = a_row.height;
		bg.clear();
		if (a_sel)
			Draw.selectBar(bg, -6, 1, a_row.width + 12, h - 2, a_focused);
		else
			Draw.divider(bg, 0, h - 1, a_row.width, 12);
		var icon: SkyIcon = a_row.icon;
		icon.clip._x = 8;
		icon.clip._y = 12;
		var known: Object = _knownById[String(a_e.id)];
		icon.show(iconLabel(known != undefined ? known : a_e), iconColor(known != undefined ? known : a_e));
		tf.text = Translator.tr(String(a_e.text));
		tf._y = tf.textHeight > 32 ? 1 : Math.round((h - tf.textHeight) / 2) - 3;
		Text.setColor(tf, a_e.canEdit == false ? Theme.TEXT_SOFT : (a_sel ? Theme.TEXT : Theme.TEXT_SOFT));

		// mouse tools on the selected row
		var tools: MovieClip = a_row.tools;
		tools._visible = a_sel && _device == "kbm";
		if (tools._visible) {
			var n: Number = _effectsList.length;
			drawTool(a_row.up, 0, h, "up", a_i > 0);
			drawTool(a_row.down, 34, h, "down", a_i < n - 1);
			drawTool(a_row.del, 68, h, "del", true);
		}
	}

	private function drawTool(a_mc: MovieClip, a_x: Number, a_h: Number, a_kind: String, a_enabled: Boolean): Void
	{
		a_mc.clear();
		var s: Number = 30;
		var y: Number = Math.round((a_h - s) / 2);
		Draw.rect(a_mc, a_x, y, s, s, 0, 0);   // hit area
		var c: Number = a_enabled ? Theme.TEXT : Theme.TEXT_DIM;
		var a: Number = a_enabled ? 85 : 40;
		if (a_kind == "up")
			Draw.triangle(a_mc, a_x + s / 2, y + s / 2, 14, -1, c, a);
		else if (a_kind == "down")
			Draw.triangle(a_mc, a_x + s / 2, y + s / 2, 14, 1, c, a);
		else
			Draw.cross(a_mc, a_x + s / 2, y + s / 2, 11, 2, c, a);
		a_mc.enabled = a_enabled;
	}

	/* SkyUI item-icon label for an effect (the DLL sends it; the school's default otherwise). */
	private static function iconLabel(a_e: Object): String
	{
		if (a_e != undefined && a_e.icon != undefined && String(a_e.icon).length > 0)
			return String(a_e.icon);
		return Theme.schoolIcon(a_e == undefined ? -1 : Number(a_e.school));
	}

	private static function iconColor(a_e: Object): Number
	{
		if (a_e != undefined && a_e.iconColor != undefined)
			return Number(a_e.iconColor);
		return 0xFFFFFF;
	}

	/* ================================================================================
	 * filtering, panes, card
	 * ================================================================================ */

	public function currentKnownId(): String
	{
		var e: Object = _knownList.selectedEntry;
		return e == undefined ? undefined : String(e.id);
	}

	public function applyFilter(a_keepId: String): Void
	{
		var school: Number = EffectFilter.schoolForTab(_tabs.selectedIndex);
		_filtered = EffectFilter.apply(_known, school, _searchBox.text);
		_knownList.emptyText = _known.length == 0 ? "$LA_UI_NoKnown" : "$LA_UI_NoMatches";
		_knownList.setData(_filtered);
		var idx: Number = StateUtil.indexOfId(_filtered, a_keepId);
		_knownList.select(idx >= 0 ? idx : 0, false, true);
		updateCard();
	}

	private function knownSchool(a_id: String): Number
	{
		var e: Object = _knownById[a_id];
		return e == undefined ? -1 : Number(e.school);
	}

	private function knownSchoolName(a_id: String): String
	{
		var e: Object = _knownById[a_id];
		return e == undefined ? "" : String(e.schoolName);
	}

	/* Rank badge colour: state.school if the DLL sends it, else the first effect's school. */
	private function spellSchoolColor(a_state: Object): Number
	{
		if (a_state.school != undefined && a_state.school >= 0)
			return Theme.schoolColor(Number(a_state.school));
		if (a_state.effects != undefined && a_state.effects.length > 0)
			return Theme.schoolColor(Number(a_state.effects[0].school));
		return Theme.NEUTRAL;
	}

	public function setPane(a_pane: String, a_silent: Boolean): Void
	{
		if (_pane == a_pane)
			return;
		_pane = a_pane;
		if (!a_silent)
			playSound(Sounds.TAB);
		updatePaneLook();
		updateCard();
		updateHints();
	}

	private function updatePaneLook(): Void
	{
		var kp: MovieClip = _knownPanel;
		if (kp["w"] == undefined)
			return;
		// Skyrim has no pane frames: the focused column's header turns white.
		Text.setColor(_knownTitleTf, _pane == "known" ? Theme.TEXT : Theme.TEXT_HINT);
		Text.setColor(_effectsTitleTf, _pane == "effects" ? Theme.TEXT : Theme.TEXT_HINT);
		_knownList.setFocused(_pane == "known");
		_effectsList.setFocused(_pane == "effects");
		_searchBox.setFocusedLook(false);
	}

	public function onListSelection(a_pane: String, a_byMouse: Boolean): Void
	{
		if (a_byMouse)
			setPane(a_pane, true);
		else
			playSound(Sounds.FOCUS);
		updateCard();
	}

	private function updateCard(): Void
	{
		var entry: Object;
		if (_pane == "effects") {
			var fx: Object = _effectsList.selectedEntry;
			if (fx != undefined) {
				entry = _knownById[String(fx.id)];
				if (entry == undefined)
					entry = {text: fx.text, school: fx.school, schoolName: "", baseCost: undefined};
			}
		} else {
			entry = _knownList.selectedEntry;
		}
		_card.setEntry(entry);
		_card.clip._visible = !_editor.isOpen && !_costMath.clip._visible;
	}

	/* ================================================================================
	 * device + hints
	 * ================================================================================ */

	public function setDevice(a_device: String): Void
	{
		_device = a_device;
		_editor.setDevice(a_device);
		_picker.setDevice(a_device);
		_loadPopup.setDevice(a_device);
		_msg.setDevice(a_device);
		updateHints();
		_effectsList.refresh();
	}

	private function updateHints(): Void
	{
		var ctx: String = _pane == "effects" ? KeyMap.CTX_EFFECTS : KeyMap.CTX_KNOWN;
		_createBtn.setGlyph(KeyMap.caption(KeyMap.CREATE, ctx, _device), _device);
		_loadBtn.setGlyph(KeyMap.caption(KeyMap.LOAD, ctx, _device), _device);
		_clearBtn.setGlyph(KeyMap.caption(KeyMap.CLEAR, ctx, _device), _device);
		_exitBtn.setGlyph(KeyMap.caption(KeyMap.CANCEL, ctx, _device), _device);
		layoutBar();

		_searchGlyph.setKey(KeyMap.caption(KeyMap.SEARCH, KeyMap.CTX_KNOWN, _device), _device);
		_searchBox.setPlaceholder(_device == "kbm" ? "$LA_UI_SearchHint" : "$LA_UI_Search");

		if (_nameBox.active && _device == "kbm") {
			_nameGlyph.setKey(undefined, _device);          // the string itself says "Enter creates"
			Text.set(_nameHintTf, "$LA_UI_EnterCreates");
		} else if (_nameBox.active) {
			_nameGlyph.setKey(KeyMap.caption(KeyMap.ACCEPT, KeyMap.CTX_TEXT, _device), _device);
			Text.set(_nameHintTf, "$LA_UI_OK");
		} else {
			_nameGlyph.setKey(KeyMap.caption(KeyMap.RENAME, KeyMap.CTX_KNOWN, _device), _device);
			Text.set(_nameHintTf, "$LA_UI_Controls_Rename");
		}
		_nameHintTf._x = _nameGlyph.clip._x + (_nameGlyph.width > 0 ? _nameGlyph.width + 8 : 0);

		var a: String = KeyMap.caption(KeyMap.MOVE_UP, KeyMap.CTX_EFFECTS, _device);
		var b: String = KeyMap.caption(KeyMap.MOVE_DOWN, KeyMap.CTX_EFFECTS, _device);
		_reorderGlyphA.setKey(a, _device);
		_reorderGlyphB.setKey(b, _device);
		_reorderGlyphB.clip._x = _reorderGlyphA.clip._x + _reorderGlyphA.width + 4;
		_reorderTf._x = _reorderGlyphB.clip._x + _reorderGlyphB.width + 8;
		Text.set(_reorderTf, "$LA_UI_Controls_Reorder");
	}

	/* ================================================================================
	 * input
	 * ================================================================================ */

	private function context(): String
	{
		if (_msg.isOpen)
			return KeyMap.CTX_MESSAGE;
		if (_nameBox.active || _searchBox.active)
			return KeyMap.CTX_TEXT;
		if (_loadPopup.isOpen || _picker.isOpen)
			return KeyMap.CTX_POPUP;
		if (_editor.isOpen)
			return KeyMap.CTX_EDITOR;
		return _pane == "effects" ? KeyMap.CTX_EFFECTS : KeyMap.CTX_KNOWN;
	}

	private static function repeatable(a_action: String): Boolean
	{
		switch (a_action) {
			case KeyMap.UP: case KeyMap.DOWN: case KeyMap.PAGE_UP: case KeyMap.PAGE_DOWN:
			case KeyMap.STEP_DEC: case KeyMap.STEP_INC: case KeyMap.BIG_DEC: case KeyMap.BIG_INC:
				return true;
		}
		return false;
	}

	/* @InputRouter */
	public function handleInput(a_d: InputDetails): Boolean
	{
		if (_closing || a_d.value == "keyUp")
			return false;
		var ctx: String = context();
		var action: String = KeyMap.resolve(ctx, a_d.code, a_d.skseKeycode, a_d.device, a_d.shift);
		if (devMode)
			trace("[LostArt] " + ctx + " " + a_d.toString() + " -> " + action);
		if (action == undefined)
			return false;
		if (a_d.value == "keyHold" && !repeatable(action))
			return false;

		switch (ctx) {
			case KeyMap.CTX_TEXT:
				return textAction(action);
			case KeyMap.CTX_MESSAGE:
				return _msg.handleAction(action);
			case KeyMap.CTX_POPUP:
				return _loadPopup.isOpen ? _loadPopup.handleAction(action) : _picker.handleAction(action);
			case KeyMap.CTX_EDITOR:
				if (action == KeyMap.COST_MATH) {
					send("LA_ToggleCostMath", []);
					return true;
				}
				return _editor.handleAction(action);
		}
		return doAction(action);
	}

	/* @InputRouter */
	public function handleWheel(a_delta: Number): Void
	{
		var rows: Number = a_delta > 0 ? -1 : 1;
		if (_msg.isOpen)
			return;
		if (_loadPopup.isOpen) {
			_loadPopup.handleWheel(a_delta);
			return;
		}
		if (_picker.isOpen) {
			_picker.handleWheel(a_delta);
			return;
		}
		if (_editor.isOpen)
			return;
		if (_knownList.containsMouse())
			_knownList.scroll(rows);
		else if (_effectsList.containsMouse())
			_effectsList.scroll(rows);
	}

	private function textAction(a_action: String): Boolean
	{
		if (_nameBox.active) {
			_nameBox.end(a_action == KeyMap.ACCEPT);
			return true;
		}
		if (_searchBox.active) {
			if (a_action == KeyMap.CANCEL)
				_searchBox.clear();
			_searchBox.end(a_action == KeyMap.ACCEPT);
			return true;
		}
		return false;
	}

	/* Main-view actions (also used by the buttons). */
	public function doAction(a_action: String): Boolean
	{
		if (_closing)
			return false;
		var list: VirtualList = _pane == "effects" ? _effectsList : _knownList;
		switch (a_action) {
			case KeyMap.UP: list.move(-1); return true;
			case KeyMap.DOWN: list.move(1); return true;
			case KeyMap.PAGE_UP: list.page(-1); return true;
			case KeyMap.PAGE_DOWN: list.page(1); return true;
			case KeyMap.HOME: list.home(); return true;
			case KeyMap.END: list.end(); return true;
			case KeyMap.LEFT: setPane("known", false); return true;
			case KeyMap.RIGHT: setPane("effects", false); return true;
			case KeyMap.TAB_PREV: _tabs.step(-1); return true;
			case KeyMap.TAB_NEXT: _tabs.step(1); return true;
			case KeyMap.MOVE_UP: moveRow(_effectsList.selectedIndex, -1); return true;
			case KeyMap.MOVE_DOWN: moveRow(_effectsList.selectedIndex, 1); return true;
			case KeyMap.ACCEPT:
				if (_pane == "effects")
					editSelected();
				else
					addSelected();
				return true;
			case KeyMap.REMOVE: removeRow(_effectsList.selectedIndex); return true;
			case KeyMap.RENAME: startNameEdit(); return true;
			case KeyMap.CREATE: send("LA_Create", []); return true;
			case KeyMap.COST_MATH: send("LA_ToggleCostMath", []); return true;
			case KeyMap.SEARCH: startSearch(); return true;
			case KeyMap.LOAD: send("LA_LoadList", []); return true;
			case KeyMap.CLEAR: send("LA_Clear", []); return true;
			case KeyMap.CANCEL:
				playSound(Sounds.CANCEL);
				send("LA_Exit", []);
				return true;
		}
		return false;
	}

	public function addSelected(): Void
	{
		var e: Object = _knownList.selectedEntry;
		if (e == undefined)
			return;
		send("LA_AddEffect", [String(e.id)]);
	}

	public function editSelected(): Void
	{
		var i: Number = _effectsList.selectedIndex;
		if (i < 0)
			return;
		send("LA_EditEffect", [i]);
	}

	public function moveRow(a_index: Number, a_delta: Number): Void
	{
		var n: Number = _effectsList.length;
		if (a_index < 0 || a_index + a_delta < 0 || a_index + a_delta >= n)
			return;
		_pendingSel = a_index + a_delta;   // the selection follows the row once the state comes back
		send("LA_MoveEffect", [a_index, a_delta]);
	}

	public function removeRow(a_index: Number): Void
	{
		if (a_index < 0 || a_index >= _effectsList.length)
			return;
		send("LA_RemoveEffect", [a_index]);
	}

	/* ---- text entry ---- */

	private function startNameEdit(): Void
	{
		_nameBox.start(_device == "kbm" ? "t" : undefined);
		if (_device == "pad")
			send("LA_RequestKeyboard", ["name", _nameBox.text, NAME_MAX_CHARS]);
	}

	private function startSearch(): Void
	{
		setPane("known", true);
		_searchBox.start(_device == "kbm" ? "/" : undefined);
		if (_device == "pad")
			send("LA_RequestKeyboard", ["search", _searchBox.text, 40]);
	}

	public function onTextStart(a_which: String): Void
	{
		playSound(Sounds.OK);
		_input.reset();
		updateHints();
	}

	public function onNameEnd(a_text: String, a_accepted: Boolean): Void
	{
		_input.reset();
		send("LA_SetName", [a_text]);
		updateHints();
		if (a_accepted)
			send("LA_Create", []);   // Morrowind: Enter in the name field tries to create
	}

	public function onSearchEnd(a_text: String, a_accepted: Boolean): Void
	{
		_input.reset();
		applyFilter(currentKnownId());
		updateHints();
	}

	public function releaseTextInput(): Void
	{
		_nameBox.end(false);
		_searchBox.end(false);
	}

	/* ================================================================================
	 * outgoing
	 * ================================================================================ */

	public function send(a_name: String, a_args: Array): Void
	{
		if (devMode)
			trace("[LostArt] -> " + a_name + " " + a_args.join(", "));
		GameDelegate.call(a_name, a_args);
	}

	public function playSound(a_id: String): Void
	{
		var now: Number = getTimer();
		var last: Number = _lastSound[a_id];
		if (last != undefined && now - last < 45)
			return;
		_lastSound[a_id] = now;
		send("LA_PlaySound", [a_id]);
	}
}
