#pragma once

// Console command `la` (hijacks an unused vanilla console command slot):
//   la menu            open the spellmaking menu (sandbox provider)
//   la test <suite>    run in-game tests (parity, effects, save, compiler, all)
//   la discover        dump the player's discovered effects and unmatched MGEFs to the log
//   la slots           print slot usage
//   la rebuild         rebuild all slots
namespace LA::Console
{
	void Install();
}
