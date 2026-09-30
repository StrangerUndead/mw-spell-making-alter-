/*
 * Bottom readouts in SkyUI's bottom-bar style: grey upper-case labels with white values on one
 * line, right-aligned: Magicka cost (counts up/down on change), Rank, Spell Chance (only when the
 * casting-failure module is on, chance >= 0), Price (red when the player cannot afford it) and
 * Gold, with the active cost model in small grey text underneath.
 * Pure rendering of state fields; no rule is evaluated here.
 */
import lostart.Theme;
import lostart.model.StateUtil;
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
			var label: TextField = Text.label(mc, "label", 1, 160, "left");
			var value: TextField = Text.create(mc, "value", 2, 0, 0, 200, 36, Theme.FS_HEADER, Theme.TEXT, Theme.FONT_MEDIUM, "left");
			value.autoSize = "left";   // the magicka value counts up; never clip it
			_cells[n] = {mc: mc, label: label, value: value};
		}
		Text.setCaps(_cells.magicka.label, "$LA_UI_Magicka");
		Text.setCaps(_cells.rank.label, "$LA_UI_Rank");
		Text.setCaps(_cells.chance.label, "$LA_UI_Chance");
		Text.setCaps(_cells.price.label, "$LA_UI_Price");
		Text.setCaps(_cells.gold.label, "$LA_UI_Gold");
		_modelTf = Text.create(clip, "model", 30, 0, 0, 300, 24, Theme.FS_LABEL, Theme.TEXT_HINT, Theme.FONT_REGULAR, "right");
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
		// Measure, then place the cells right to left so the gold ends at the bar's edge.
		var gap: Number = 38;
		var order: Array = ["magicka", "rank", "chance", "price", "gold"];
		var widths: Array = [];
		var total: Number = 0;
		for (var i: Number = 0; i < order.length; i++) {
			var c: Object = _cells[order[i]];
			if (!c.mc._visible) {
				widths.push(0);
				continue;
			}
			var lw: Number = c.label.textWidth + 4;
			var vw: Number = c.value.textWidth + 4;
			c.label._width = lw + 4;
			if (order[i] == "magicka")
				vw = Math.max(vw, 64);   // room for the count-up without moving the other cells
			c.label._x = 0;
			c.value._x = lw + 8;
			var w: Number = lw + 8 + vw;
			widths.push(w);
			total += w + (total > 0 ? gap : 0);
		}
		var lineY: Number = Math.round((_h - 36) / 2) - 8;
		var x: Number = Math.max(0, _w - total);
		for (var j: Number = 0; j < order.length; j++) {
			var cell: Object = _cells[order[j]];
			if (!cell.mc._visible)
				continue;
			cell.mc._x = x;
			cell.mc._y = lineY;
			cell.label._y = Math.round((36 - cell.label.textHeight) / 2) + 1;
			cell.value._y = Math.round((36 - cell.value.textHeight) / 2) - 3;
			x += widths[j] + gap;
		}
		_badge.clear();
		_modelTf._x = 0;
		_modelTf._width = _w;
		_modelTf._y = lineY + 36;
	}
}
