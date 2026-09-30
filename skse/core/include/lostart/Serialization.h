#pragma once

#include "lostart/Types.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace LA
{
	// Co-save ids (SKSE record types are big-endian four-character codes).
	constexpr std::uint32_t FourCC(const char (&a_code)[5])
	{
		return (static_cast<std::uint32_t>(a_code[0]) << 24) | (static_cast<std::uint32_t>(a_code[1]) << 16) |
		       (static_cast<std::uint32_t>(a_code[2]) << 8) | static_cast<std::uint32_t>(a_code[3]);
	}
	inline constexpr std::uint32_t kCosaveId = FourCC("LART");
	inline constexpr std::uint32_t kRecordDefinitions = FourCC("LADF");
	inline constexpr std::uint32_t kRecordMarks = FourCC("LAMK");
	inline constexpr std::uint32_t kRecordLedger = FourCC("LALG");
	inline constexpr std::uint32_t kRecordConditions = FourCC("LACP");
	inline constexpr std::uint32_t kRecordVersion = FourCC("LAVR");
	inline constexpr std::uint32_t kSchemaVersion = 1;

	class ByteWriter
	{
	public:
		void U8(std::uint8_t a_v) { _bytes.push_back(a_v); }
		void U16(std::uint16_t a_v);
		void U32(std::uint32_t a_v);
		void F32(float a_v);
		void VarU(std::uint64_t a_v);
		void VarS(std::int64_t a_v);
		void Str(std::string_view a_s);
		const std::vector<std::uint8_t>& Bytes() const { return _bytes; }
		std::vector<std::uint8_t>        Take() { return std::move(_bytes); }

	private:
		std::vector<std::uint8_t> _bytes;
	};

	class ByteReader
	{
	public:
		explicit ByteReader(const std::vector<std::uint8_t>& a_bytes) : _bytes(a_bytes) {}
		bool Ok() const { return _ok; }
		bool AtEnd() const { return _pos >= _bytes.size(); }

		std::uint8_t  U8();
		std::uint16_t U16();
		std::uint32_t U32();
		float         F32();
		std::uint64_t VarU();
		std::int64_t  VarS();
		std::string   Str();

	private:
		bool Need(std::size_t a_n);

		const std::vector<std::uint8_t>& _bytes;
		std::size_t                      _pos{ 0 };
		bool                             _ok{ true };
	};

	struct Mark
	{
		std::uint32_t        cell{ 0 };        // interior cell (full FormID) or 0
		std::uint32_t        worldspace{ 0 };  // exterior worldspace or 0
		std::array<float, 3> pos{};
		float                angleZ{ 0 };
		bool operator==(const Mark&) const = default;
	};

	// Attribute damage per actor (Damage Attribute persists until restored).
	struct LedgerEntry
	{
		std::uint32_t                        actor{ 0 };
		std::array<float, kAttributeCount>   damage{};
		bool operator==(const LedgerEntry&) const = default;
	};

	// Disintegrate condition pool per actor and item (100 = pristine).
	struct ConditionEntry
	{
		std::uint32_t actor{ 0 };
		std::uint32_t item{ 0 };
		float         condition{ 100.0f };
		bool operator==(const ConditionEntry&) const = default;
	};

	struct VersionInfo
	{
		std::uint32_t                                   schema{ kSchemaVersion };
		std::vector<std::pair<std::uint32_t, float>>    history;  // schema, game days when migrated
		bool operator==(const VersionInfo&) const = default;
	};

	// Form ids are written as saved; on load the plugin maps them through SKSE's ResolveFormID
	// (returns nullopt when the form's plugin is gone).
	using FormResolver = std::function<std::optional<std::uint32_t>(std::uint32_t)>;

	namespace Serial
	{
		std::vector<std::uint8_t> EncodeDefinitions(const std::vector<SpellDef>& a_defs);
		bool DecodeDefinitions(const std::vector<std::uint8_t>& a_bytes, std::uint32_t a_version, std::vector<SpellDef>& a_out,
			const FormResolver& a_resolve = {});

		std::vector<std::uint8_t> EncodeMarks(const std::vector<Mark>& a_marks);
		bool DecodeMarks(const std::vector<std::uint8_t>& a_bytes, std::uint32_t a_version, std::vector<Mark>& a_out,
			const FormResolver& a_resolve = {});

		std::vector<std::uint8_t> EncodeLedger(const std::vector<LedgerEntry>& a_entries);
		bool DecodeLedger(const std::vector<std::uint8_t>& a_bytes, std::uint32_t a_version, std::vector<LedgerEntry>& a_out,
			const FormResolver& a_resolve = {});

		std::vector<std::uint8_t> EncodeConditions(const std::vector<ConditionEntry>& a_entries);
		bool DecodeConditions(const std::vector<std::uint8_t>& a_bytes, std::uint32_t a_version, std::vector<ConditionEntry>& a_out,
			const FormResolver& a_resolve = {});

		std::vector<std::uint8_t> EncodeVersion(const VersionInfo& a_info);
		bool DecodeVersion(const std::vector<std::uint8_t>& a_bytes, std::uint32_t a_version, VersionInfo& a_out);
	}
}
