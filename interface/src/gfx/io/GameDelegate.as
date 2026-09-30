/*
 * Lost Art of Spellmaking - clean-room GameDelegate.
 *
 * Skyrim's own menus talk to the engine through a class called gfx.io.GameDelegate
 * (part of Scaleform CLIK, shipped inside Skyrim's SWFs; not redistributable). This file is an
 * independent implementation of the same *wire protocol* so that the SKSE plugin can register
 * its handlers with RE::FxDelegate exactly as for a vanilla menu:
 *
 *   SWF -> engine : ExternalInterface.call(methodName, responseId, arg0, arg1, ...)
 *                   FxDelegate strips responseId and dispatches methodName to the registered
 *                   C++ callback (FxDelegateArgs[0..n]).
 *   engine -> SWF : ExternalInterface callbacks "call" (methodName, args...) and
 *                   "respond" (responseId, args...).
 *
 * Only the protocol is shared with CLIK; no CLIK code is included.
 */
import flash.external.ExternalInterface;

class gfx.io.GameDelegate
{
	private static var _responses: Object = {};
	private static var _callbacks: Object = {};
	private static var _nextId: Number = 0;
	private static var _ready: Boolean = false;

	/* Optional observer, used by the offline harness / debug overlay: fn(methodName, argsArray). */
	public static var tap: Function;

	/*
	 * Sends methodName with the given arguments to the engine.
	 * a_scope/a_callback (optional) receive a synchronous "respond" from C++.
	 */
	public static function call(a_methodName: String, a_params: Array, a_scope: Object, a_callback: String): Void
	{
		if (!_ready)
			initialize();

		var id: Number = ++_nextId;
		_responses[id] = [a_scope, a_callback];

		var args: Array = [a_methodName, id];
		if (a_params != undefined) {
			for (var i: Number = 0; i < a_params.length; i++)
				args.push(a_params[i]);
		}

		if (tap != undefined)
			tap(a_methodName, a_params);

		ExternalInterface.call.apply(null, args);
		delete _responses[id];
	}

	public static function addCallBack(a_methodName: String, a_scope: Object, a_callback: String): Void
	{
		if (!_ready)
			initialize();
		_callbacks[a_methodName] = [a_scope, a_callback];
	}

	public static function removeCallBack(a_methodName: String): Void
	{
		delete _callbacks[a_methodName];
	}

	public static function receiveCall(a_methodName: String): Void
	{
		var entry: Array = _callbacks[a_methodName];
		if (entry == undefined)
			return;
		var scope: Object = entry[0];
		var fn: Function = scope[entry[1]];
		fn.apply(scope, arguments.slice(1));
	}

	public static function receiveResponse(a_id: Number): Void
	{
		var entry: Array = _responses[a_id];
		if (entry == undefined)
			return;
		var scope: Object = entry[0];
		var fn: Function = scope[entry[1]];
		fn.apply(scope, arguments.slice(1));
	}

	public static function initialize(): Void
	{
		_ready = true;
		ExternalInterface.addCallback("call", GameDelegate, receiveCall);
		ExternalInterface.addCallback("respond", GameDelegate, receiveResponse);
	}
}
