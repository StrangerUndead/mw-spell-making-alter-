/*
 * Bottom readouts: Magicka cost (counts up/down on change), Rank badge in the school colour,
 * Spell Chance (only when the casting-failure module is on, chance >= 0), Price (red when the
 * player cannot afford it) and Your Gold, plus the active cost model name.
 * Pure rendering of state fields; no rule is evaluated here.
 */
import lostart.Theme;
import lostart.model.StateUtil;
import lostart.util.Draw;
import lostart.util.Text;
import lostart.util.Translator;
import lostart.util.Tween;

class lostart.components.ReadoutBar
{
	public var clip: MovieClip;

	private var _cells: Object;
	private var _badge: MovieClip;
	private var _tweenMc: MovieClip;
	private var _modelTf: TextField;
	private var _w: Number = 800;
	private var _h: Number = 72;
	private var _shownCost: Number;
	private var _costText: String = "";
	private var _state: Object;
	private var _schoolColor: Number;

	public function ReadoutBar(a_parent: MovieClip, a_name: String, a_depth: Number)
	{
		clip = a_parent.createEmptyMovieClip(a_name, a_depth);
		_tweenMc = clip.createEmptyMovieClip("tween", 1);
		_badge = clip.createEmptyMovieClip("badge", 2);
		_cells = {};
		var names: Array = ["magicka", "rank", "chance", "price", "gold"];
		for (var i: Number = 0; i < names.length; i++) {
			var n: String = names[i];
			var mc: MovieClip = clip.createEmptyMovieClip("cell_" + n, 10 + i);
			var label: TextField = Text.create(mc, "label", 1, 0, 0, 160, 24, Theme.FS_HINT, Theme.TEXT_HINT, Theme.FONT_MEDIUM, "left");
			var value: TextField = Text.create(mc, "value", 2, 0, 22, 200, 40, Theme.FS_TITLE, Theme.TEXT, Theme.FONT_MEDIUM, "left");
			_cells[n] = {mc: mc, label: label, value: value};
		}
		Text.set(_cells.magicka.label, "$LA_UI_MagickaCost");
		Text.set(_cells.rank.label, "$LA_UI_Rank");
		Text.set(_cells.chance.label, "$LA_UI_SpellChance");
		Text.set(_cells.price.label, "$LA_UI_Price");
		Text.set(_cells.gold.label, "$LA_UI_YourGold");
		_modelTf = Text.create(clip, "model", 30, 0, 0, 300, 24, Theme.FS_HINT, Theme.TEXT_HINT, Theme.FONT_REGULAR, "left");
	}

	public function setSize(a_w: Number, a_h: Number): Void
	{
		_w = a_w;
		_h = a_h;
		layout();
	}

	/* a_schoolColor: badge colour (spell's school), a_animate: count the cost up/down. */
	public function setState(a_state: Object, a_schoolColor: Number, a_animate: Boolean): Void
	{
		var self: ReadoutBar = this;
		_state = a_state;
		_schoolColor = a_schoolColor;
		var cost: Number = StateUtil.num(a_state.cost, 0);
		_costText = a_state.costText == undefined ? String(cost) : Translator.tr(String(a_state.costText));
		if (_shownCost == undefined || !a_animate) {
			Tween.stop(_tweenMc);
			_shownCost = cost;
			_cells.magicka.value.text = _costText;
		} else if (cost != _shownCost) {
			var from: Number = _shownCost;
			_shownCost = cost;
			Tween.run(_tweenMc, from, cost, Math.min(400, 120 + Math.abs(cost - from) * 4),
				function(v: Number): Void { self.showCost(Math.round(v)); },
				function(): Void { self.showCostText(); });
		} else {
			_cells.magicka.value.text = _costText;
		}

		var rankName: String = StateUtil.str(a_state.rankName);
		_cells.rank.value.text = Translator.tr(rankName);

		var chance: Number = StateUtil.num(a_state.chance, -1);
		_cells.chance.mc._visible = chance >= 0;
		_cells.chance.value.text = String(Math.round(chance)) + "%";

		var price: Number = StateUtil.num(a_state.price, 0);
		_cells.price.value.text = String(price);
		Text.setColor(_cells.price.value, a_state.canAfford == false ? Theme.TEXT_RED : Theme.TEXT);

		_cells.gold.value.text = String(StateUtil.num(a_state.gold, 0));

		var model: String = StateUtil.str(a_state.modelName);
		_modelTf.text = model.length > 0 ? Translator.format("$LA_UI_CostModelFmt", [Translator.tr(model)]) : "";
		layout();
	}

	public function showCost(a_value: Number): Void
	{
		_cells.magicka.value.text = String(a_value);
	}

	public function showCostText(): Void
	{
		_cells.magicka.value.text = _costText;
	}

	private function layout(): Void
	{
		var x: Number = 0;
		var gap: Number = 44;
		var order: Array = ["magicka", "rank", "chance", "price", "gold"];
		for (var i: Number = 0; i < order.length; i++) {
			var c: Object = _cells[order[i]];
			var mc: MovieClip = c.mc;
			if (!mc._visible)
				continue;
			mc._x = x;
			mc._y = Math.round((_h - 62) / 2);
			var vw: Number = Math.max(c.value.textWidth, c.label.textWidth) + 8;
			if (order[i] == "rank")
				vw += 24;
			c.value._width = vw + 4;
			c.label._width = vw + 4;
			if (order[i] == "rank") {
				c.value._x = 12;
				drawBadge(mc._x, mc._y + 24, c.value.textWidth + 24, 36);
			}
			x += vw + gap;
		}
		_modelTf._x = x;
		_modelTf._width = Math.max(20, _w - x);
		_modelTf._y = Math.round((_h - 62) / 2) + 34;
	}

	private function drawBadge(a_x: Number, a_y: Number, a_w: Number, a_h: Number): Void
	{
		_badge.clear();
		var c: Number = _schoolColor == undefined ? Theme.NEUTRAL : _schoolColor;
		Draw.roundRect(_badge, a_x, a_y, a_w, a_h, 6, c, 30);
		Draw.roundFrame(_badge, a_x, a_y, a_w, a_h, 6, 2, c, 100);
	}
}
