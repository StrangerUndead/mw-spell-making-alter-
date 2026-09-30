#include "lostart/Serialization.h"

#include <algorithm>
#include <cstring>

namespace LA
{
	void ByteWriter::U16(std::uint16_t a_v)
	{
		U8(static_cast<std::uint8_t>(a_v & 0xFF));
		U8(static_cast<std::uint8_t>(a_v >> 8));
	}

	void ByteWriter::U32(std::uint32_t a_v)
	{
		for (int i = 0; i < 4; ++i) {
			U8(static_cast<std::uint8_t>((a_v >> (8 * i)) & 0xFF));
		}
	}

	void ByteWriter::F32(float a_v)
	{
		std::uint32_t bits = 0;
		std::memcpy(&bits, &a_v, sizeof(bits));
		U32(bits);
	}

	void ByteWriter::VarU(std::uint64_t a_v)
	{
		do {
			std::uint8_t byte = a_v & 0x7F;
			a_v >>= 7;
			if (a_v) {
				byte |= 0x80;
			}
			U8(byte);
		} while (a_v);
	}

	void ByteWriter::VarS(std::int64_t a_v)
	{
		// Zig-zag so -1 costs one byte.
		VarU((static_cast<std::uint64_t>(a_v) << 1) ^ static_cast<std::uint64_t>(a_v >> 63));
	}

	void ByteWriter::Str(std::string_view a_s)
	{
		VarU(a_s.size());
		_bytes.insert(_bytes.end(), a_s.begin(), a_s.end());
	}

	bool ByteReader::Need(std::size_t a_n)
	{
		if (!_ok || _pos + a_n > _bytes.size()) {
			_ok = false;
			return false;
		}
		return true;
	}

	std::uint8_t ByteReader::U8()
	{
		return Need(1) ? _bytes[_pos++] : 0;
	}

	std::uint16_t ByteReader::U16()
	{
		if (!Need(2)) {
			return 0;
		}
		const auto v = static_cast<std::uint16_t>(_bytes[_pos] | (_bytes[_pos + 1] << 8));
		_pos += 2;
		return v;
	}

	std::uint32_t ByteReader::U32()
	{
		if (!Need(4)) {
			return 0;
		}
		std::uint32_t v = 0;
		for (int i = 0; i < 4; ++i) {
			v |= static_cast<std::uint32_t>(_bytes[_pos + i]) << (8 * i);
		}
		_pos += 4;
		return v;
	}

	float ByteReader::F32()
	{
		const auto bits = U32();
		float      v = 0;
		std::memcpy(&v, &bits, sizeof(v));
		return v;
	}

	std::uint64_t ByteReader::VarU()
	{
		std::uint64_t v = 0;
		for (int shift = 0; shift < 64; shift += 7) {
			const auto byte = U8();
			if (!_ok) {
				return 0;
			}
			v |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
			if (!(byte & 0x80)) {
				return v;
			}
		}
		_ok = false;
		return 0;
	}

	std::int64_t ByteReader::VarS()
	{
		const auto u = VarU();
		return static_cast<std::int64_t>(u >> 1) ^ -static_cast<std::int64_t>(u & 1);
	}

	std::string ByteReader::Str()
	{
		const auto len = VarU();
		if (!Need(static_cast<std::size_t>(len))) {
			return {};
		}
		std::string s(reinterpret_cast<const char*>(_bytes.data() + _pos), static_cast<std::size_t>(len));
		_pos += static_cast<std::size_t>(len);
		return s;
	}

	namespace Serial
	{
		namespace
		{
			std::uint32_t Resolve(const FormResolver& a_resolve, std::uint32_t a_id)
			{
				if (!a_resolve || a_id == 0) {
					return a_id;
				}
				return a_resolve(a_id).value_or(0);
			}
		}

		// LADF v1: string table of effect ids, then each definition with varint fields.
		std::vector<std::uint8_t> EncodeDefinitions(const std::vector<SpellDef>& a_defs)
		{
			std::vector<std::string> ids;
			auto indexOf = [&](const std::string& a_id) {
				const auto it = std::find(ids.begin(), ids.end(), a_id);
				if (it != ids.end()) {
					return static_cast<std::uint64_t>(it - ids.begin());
				}
				ids.push_back(a_id);
				return static_cast<std::uint64_t>(ids.size() - 1);
			};
			for (const auto& def : a_defs) {
				for (const auto& e : def.effects) {
					indexOf(e.effectId);
				}
			}

			ByteWriter w;
			w.VarU(ids.size());
			for (const auto& id : ids) {
				w.Str(id);
			}
			w.VarU(a_defs.size());
			for (const auto& def : a_defs) {
				w.VarU(def.slot);
				w.Str(def.name);
				w.VarU(def.effects.size());
				for (const auto& e : def.effects) {
					w.VarU(indexOf(e.effectId));
					w.VarS(e.sub);
					w.U8(static_cast<std::uint8_t>(e.range));
					w.VarU(e.minMag);
					w.VarU(e.maxMag);
					w.VarU(e.duration);
					w.VarU(e.area);
				}
				w.U8(static_cast<std::uint8_t>(def.costModel));
				w.VarU(def.cost);
				w.VarU(def.pricePaid);
				w.F32(def.created);
				w.U32(def.provider);
				w.VarU(def.flags);
				w.VarU(def.subSlots.size());
				for (auto slot : def.subSlots) {
					w.VarU(slot);
				}
			}
			return w.Take();
		}

		bool DecodeDefinitions(const std::vector<std::uint8_t>& a_bytes, std::uint32_t a_version, std::vector<SpellDef>& a_out,
			const FormResolver& a_resolve)
		{
			if (a_version == 0 || a_version > kSchemaVersion) {
				return false;
			}
			ByteReader               r(a_bytes);
			std::vector<std::string> ids(static_cast<std::size_t>(std::min<std::uint64_t>(r.VarU(), 65535)));
			for (auto& id : ids) {
				id = r.Str();
			}
			const auto count = r.VarU();
			std::vector<SpellDef> defs;
			for (std::uint64_t i = 0; i < count && r.Ok(); ++i) {
				SpellDef def;
				def.slot = static_cast<std::uint16_t>(r.VarU());
				def.name = r.Str();
				const auto effects = r.VarU();
				for (std::uint64_t j = 0; j < effects && r.Ok(); ++j) {
					SpellEffect e;
					const auto  index = r.VarU();
					e.effectId = index < ids.size() ? ids[static_cast<std::size_t>(index)] : std::string();
					e.sub = static_cast<std::int16_t>(r.VarS());
					e.range = static_cast<Range>(std::min<std::uint8_t>(r.U8(), 2));
					e.minMag = static_cast<std::uint16_t>(r.VarU());
					e.maxMag = static_cast<std::uint16_t>(r.VarU());
					e.duration = static_cast<std::uint16_t>(r.VarU());
					e.area = static_cast<std::uint16_t>(r.VarU());
					def.effects.push_back(std::move(e));
				}
				def.costModel = static_cast<CostModel>(std::min<std::uint8_t>(r.U8(), 2));
				def.cost = static_cast<std::uint32_t>(r.VarU());
				def.pricePaid = static_cast<std::uint32_t>(r.VarU());
				def.created = r.F32();
				def.provider = Resolve(a_resolve, r.U32());
				def.flags = static_cast<std::uint32_t>(r.VarU());
				const auto subs = r.VarU();
				for (std::uint64_t j = 0; j < subs && r.Ok(); ++j) {
					def.subSlots.push_back(static_cast<std::uint16_t>(r.VarU()));
				}
				defs.push_back(std::move(def));
			}
			if (!r.Ok()) {
				return false;
			}
			a_out = std::move(defs);
			return true;
		}

		std::vector<std::uint8_t> EncodeMarks(const std::vector<Mark>& a_marks)
		{
			ByteWriter w;
			w.VarU(a_marks.size());
			for (const auto& m : a_marks) {
				w.U32(m.cell);
				w.U32(m.worldspace);
				for (float p : m.pos) {
					w.F32(p);
				}
				w.F32(m.angleZ);
			}
			return w.Take();
		}

		bool DecodeMarks(const std::vector<std::uint8_t>& a_bytes, std::uint32_t a_version, std::vector<Mark>& a_out,
			const FormResolver& a_resolve)
		{
			if (a_version == 0 || a_version > kSchemaVersion) {
				return false;
			}
			ByteReader        r(a_bytes);
			std::vector<Mark> marks;
			const auto        count = r.VarU();
			for (std::uint64_t i = 0; i < count && r.Ok(); ++i) {
				Mark m;
				const auto cell = r.U32();
				const auto world = r.U32();
				m.cell = Resolve(a_resolve, cell);
				m.worldspace = Resolve(a_resolve, world);
				for (float& p : m.pos) {
					p = r.F32();
				}
				m.angleZ = r.F32();
				// A mark whose cell or worldspace vanished with its plugin is dropped.
				if ((cell != 0 && m.cell == 0) || (world != 0 && m.worldspace == 0)) {
					continue;
				}
				marks.push_back(m);
			}
			if (!r.Ok()) {
				return false;
			}
			a_out = std::move(marks);
			return true;
		}

		std::vector<std::uint8_t> EncodeLedger(const std::vector<LedgerEntry>& a_entries)
		{
			ByteWriter w;
			w.VarU(a_entries.size());
			for (const auto& e : a_entries) {
				w.U32(e.actor);
				for (float d : e.damage) {
					w.F32(d);
				}
			}
			return w.Take();
		}

		bool DecodeLedger(const std::vector<std::uint8_t>& a_bytes, std::uint32_t a_version, std::vector<LedgerEntry>& a_out,
			const FormResolver& a_resolve)
		{
			if (a_version == 0 || a_version > kSchemaVersion) {
				return false;
			}
			ByteReader               r(a_bytes);
			std::vector<LedgerEntry> entries;
			const auto               count = r.VarU();
			for (std::uint64_t i = 0; i < count && r.Ok(); ++i) {
				LedgerEntry e;
				e.actor = Resolve(a_resolve, r.U32());
				for (float& d : e.damage) {
					d = r.F32();
				}
				if (e.actor != 0) {
					entries.push_back(e);
				}
			}
			if (!r.Ok()) {
				return false;
			}
			a_out = std::move(entries);
			return true;
		}

		std::vector<std::uint8_t> EncodeConditions(const std::vector<ConditionEntry>& a_entries)
		{
			ByteWriter w;
			w.VarU(a_entries.size());
			for (const auto& e : a_entries) {
				w.U32(e.actor);
				w.U32(e.item);
				w.F32(e.condition);
			}
			return w.Take();
		}

		bool DecodeConditions(const std::vector<std::uint8_t>& a_bytes, std::uint32_t a_version, std::vector<ConditionEntry>& a_out,
			const FormResolver& a_resolve)
		{
			if (a_version == 0 || a_version > kSchemaVersion) {
				return false;
			}
			ByteReader                  r(a_bytes);
			std::vector<ConditionEntry> entries;
			const auto                  count = r.VarU();
			for (std::uint64_t i = 0; i < count && r.Ok(); ++i) {
				ConditionEntry e;
				e.actor = Resolve(a_resolve, r.U32());
				e.item = Resolve(a_resolve, r.U32());
				e.condition = r.F32();
				if (e.actor != 0 && e.item != 0) {
					entries.push_back(e);
				}
			}
			if (!r.Ok()) {
				return false;
			}
			a_out = std::move(entries);
			return true;
		}

		std::vector<std::uint8_t> EncodeVersion(const VersionInfo& a_info)
		{
			ByteWriter w;
			w.U32(a_info.schema);
			w.VarU(a_info.history.size());
			for (const auto& [schema, days] : a_info.history) {
				w.U32(schema);
				w.F32(days);
			}
			return w.Take();
		}

		bool DecodeVersion(const std::vector<std::uint8_t>& a_bytes, std::uint32_t a_version, VersionInfo& a_out)
		{
			if (a_version == 0 || a_version > kSchemaVersion) {
				return false;
			}
			ByteReader  r(a_bytes);
			VersionInfo info;
			info.schema = r.U32();
			const auto count = r.VarU();
			for (std::uint64_t i = 0; i < count && r.Ok(); ++i) {
				const auto schema = r.U32();
				const auto days = r.F32();
				info.history.emplace_back(schema, days);
			}
			if (!r.Ok()) {
				return false;
			}
			a_out = std::move(info);
			return true;
		}
	}
}
