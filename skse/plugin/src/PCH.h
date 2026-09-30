#pragma once

// Precompiled header for LostArt.dll (CommonLibSSE-NG, SE + AE).

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "lostart/Catalog.h"
#include "lostart/Compiler.h"
#include "lostart/Content.h"
#include "lostart/CostEngine.h"
#include "lostart/Discovery.h"
#include "lostart/Mechanics.h"
#include "lostart/MenuSession.h"
#include "lostart/Serialization.h"
#include "lostart/Settings.h"
#include "lostart/SlotPool.h"
#include "lostart/Text.h"
#include "lostart/Types.h"
#include "lostart/Util.h"

using namespace std::literals;

namespace logger = SKSE::log;

namespace stl
{
	using namespace SKSE::stl;

	// Writes a_newFunc into slot a_index of a_vtbl, storing the original in T::func.
	template <class T>
	void write_vfunc(REL::VariantID a_vtbl, std::size_t a_index = T::idx)
	{
		REL::Relocation<std::uintptr_t> vtbl{ a_vtbl };
		T::func = vtbl.write_vfunc(a_index, T::thunk);
	}

	template <class F, class T>
	void write_vfunc(std::size_t a_index = T::idx)
	{
		REL::Relocation<std::uintptr_t> vtbl{ F::VTABLE[0] };
		T::func = vtbl.write_vfunc(a_index, T::thunk);
	}
}
