#ifndef SIGNALSMITH_CBOR_WALKER_H
#define SIGNALSMITH_CBOR_WALKER_H

#include <cstdint>
#include <cmath>
#ifndef UINT64_MAX
#	define UINT64_MAX 0xFFFFFFFFFFFFFFFFull;
#endif

#if __cplusplus >= 201703L
#	define CBOR_WALKER_USE_STRING_VIEW
#	include <string_view>
#endif

namespace signalsmith { namespace cbor {

struct CborWalker {
	CborWalker() : CborWalker(nullptr, nullptr, ERROR_NOT_INITIALISED) {}
	CborWalker(const unsigned char *data, const unsigned char *dataEnd) : data(data), dataEnd(dataEnd) {
		if (data >= dataEnd) {
			typeCode = TypeCode::error;
			additional = ERROR_END_OF_DATA;
			return;
		}
		unsigned char head = *data;
		typeCode = (TypeCode)(head>>5);
		unsigned char remainder = head&0x1F;
		switch (remainder) {
		case 24:
			additional = data[1];
			dataNext = data + 2;
			break;
		case 25:
			if (typeCode == TypeCode::simple) {
				// Translated from RFC 8949 Appendix D
				uint16_t half = ((uint16_t)data[1]<<8) + data[2];
				uint16_t exponent = (half>>10)&0x001F;
				uint16_t mantissa = half&0x03FF;
				double value;
				if (exponent == 0) {
					value = std::ldexpf(mantissa, -24);
				} else if (exponent == 31) {
					value = (mantissa == 0) ? INFINITY : NAN;
				} else {
					value = std::ldexpf(mantissa + 1024, exponent - 25);
				}
				typeCode = TypeCode::float32;
				float32 = (half&0x8000) ? -value : value;
			} else {
				additional = (uint64_t(data[1])<<8)|uint64_t(data[2]);
			}
			dataNext = data + 3;
			break;
		case 26:
			if (typeCode == TypeCode::simple) {
				typeCode = TypeCode::float32;
			}
			additional = (uint64_t(data[1])<<24)|(uint64_t(data[2])<<16)|(uint64_t(data[3])<<8)|uint64_t(data[4]);
			dataNext = data + 5;
			break;
		case 27:
			if (typeCode == TypeCode::simple) {
				typeCode = TypeCode::float64;
			}
			additional = (uint64_t(data[1])<<56)|(uint64_t(data[2])<<48)|(uint64_t(data[3])<<40)|(uint64_t(data[4])<<32)|(uint64_t(data[5])<<24)|(uint64_t(data[6])<<16)|(uint64_t(data[7])<<8)|uint64_t(data[8]);
			dataNext = data + 9;
			break;
		case 28:
		case 29:
		case 30:
			typeCode = TypeCode::error;
			additional = ERROR_INVALID_ADDITIONAL;
			dataNext = data;
		case 31:
			additional = 0; // returns 0 length for indefinite values
			switch (typeCode) {
			case TypeCode::integerP:
			case TypeCode::integerN:
			case TypeCode::tag:
				typeCode = TypeCode::error;
				additional = ERROR_INVALID_ADDITIONAL;
				break;
			case TypeCode::bytes:
				typeCode = TypeCode::indefiniteBytes;
				break;
			case TypeCode::utf8:
				typeCode = TypeCode::indefiniteUtf8;
				break;
			case TypeCode::array:
				typeCode = TypeCode::indefiniteArray;
				break;
			case TypeCode::map:
				typeCode = TypeCode::indefiniteMap;
				break;
			case TypeCode::simple:
				typeCode = TypeCode::indefiniteBreak;
				break;
			default:
				typeCode = TypeCode::error;
				additional = ERROR_SHOULD_BE_IMPOSSIBLE;
				break;
			}
		default:
			additional = remainder;
			dataNext = data + 1;
			break;
		}
	}
	
	// All error codes are positive, so can be checked with `.error()`
	static constexpr uint64_t ERROR_END_OF_DATA = 1;
	static constexpr uint64_t ERROR_INVALID_ADDITIONAL = 2;
	static constexpr uint64_t ERROR_INVALID_VALUE = 3;
	static constexpr uint64_t ERROR_INCONSISTENT_INDEFINITE = 4;
	static constexpr uint64_t ERROR_NOT_INITIALISED = 5;
	static constexpr uint64_t ERROR_METHOD_TYPE_MISMATCH = 6;
	static constexpr uint64_t ERROR_SHOULD_BE_IMPOSSIBLE = 7;

	CborWalker next(size_t count) const {
		CborWalker result = *this;
		for (size_t i = 0; i < count; ++i) {
			result = result.next();
		}
		return result;
	}
	CborWalker next() const {
		switch (typeCode) {
		case TypeCode::integerP:
		case TypeCode::integerN:
		case TypeCode::simple:
		case TypeCode::float32:
		case TypeCode::float64:
		case TypeCode::indefiniteBreak:
			return nextBasic();
		case TypeCode::bytes:
		case TypeCode::utf8:
			return {dataNext + additional, dataEnd};
			return {dataNext + additional, dataEnd};
		case TypeCode::array: {
			auto result = nextBasic();
			auto length = additional;
			for (uint64_t i = 0; i < length; ++i) {
				result = result.next();
			}
			return result;
		}
		case TypeCode::map: {
			auto result = nextBasic();
			auto length = additional;
			for (uint64_t i = 0; i < length; ++i) {
				result = result.next();
			}
			return result;
		}
		case TypeCode::indefiniteBytes: {
			auto result = nextBasic();
			while (!result.error() && result.typeCode != TypeCode::indefiniteBreak) {
				if (result.typeCode != TypeCode::bytes) {
					return {data, dataEnd, ERROR_INCONSISTENT_INDEFINITE};
				}
				result = result.next();
			}
			return result.nextBasic();
		}
		case TypeCode::indefiniteUtf8: {
			auto result = nextBasic();
			while (!result.error() && result.typeCode != TypeCode::indefiniteBreak) {
				if (result.typeCode != TypeCode::utf8) {
					return {data, dataEnd, ERROR_INCONSISTENT_INDEFINITE};
				}
				result = result.next();
			}
			return result.nextBasic();
		}
		case TypeCode::indefiniteArray: {
			auto result = nextBasic();
			while (!result.error() && result.typeCode != TypeCode::indefiniteBreak) {
				result = result.next();
			}
			return result.nextBasic();
		}
		case TypeCode::indefiniteMap: {
			auto result = nextBasic();
			while (!result.error() && result.typeCode != TypeCode::indefiniteBreak) {
				result = result.next();
			}
			return result.nextBasic();
		}
		case TypeCode::tag: {
			// Skip all the tags first
			auto result = nextBasic();
			while (result.isTagged()) result = nextBasic();
			return result.next();
		}
		case TypeCode::error:
			return *this;
		}
	}

	CborWalker enter() const {
		switch (typeCode) {
		case TypeCode::integerP:
		case TypeCode::integerN:
		case TypeCode::simple:
		case TypeCode::float32:
		case TypeCode::float64:
		case TypeCode::indefiniteBreak:
		case TypeCode::bytes:
		case TypeCode::utf8:
			return next();
		case TypeCode::tag:
		case TypeCode::array:
		case TypeCode::map:
		case TypeCode::indefiniteBytes:
		case TypeCode::indefiniteUtf8:
		case TypeCode::indefiniteArray:
		case TypeCode::indefiniteMap:
			return nextBasic();
		case TypeCode::error:
			return *this;
		}
	}

	CborWalker nextExit() const {
		CborWalker result = *this;
		while (!result.error() && !result.isExit()) {
			result = result.next();
		}
		return result.nextBasic();
	}

	uint64_t error() const {
		return typeCode == TypeCode::error ? additional : 0;
	}

	bool isSimple() const {
		return typeCode == TypeCode::simple;
	}
	bool isBool() const {
		if (typeCode != TypeCode::simple) return false;
		return (additional == 20 || additional == 21);
	}
	explicit operator bool() const {
		return (additional == 21);
	}

	bool isNull() const {
		return typeCode == TypeCode::simple && additional == 22;
	}

	bool isUndefined() const {
		return typeCode == TypeCode::simple && additional == 23;
	}

	bool isExit() const {
		return typeCode == TypeCode::indefiniteBreak;
	}
	
	bool atEnd() const {
		return typeCode == TypeCode::error && additional == ERROR_END_OF_DATA;
	}

	bool isInt() const {
		return typeCode == TypeCode::integerP || typeCode == TypeCode::integerN;
	}
	operator uint64_t() const {
		switch (typeCode) {
			case TypeCode::integerP:
			case TypeCode::bytes:
			case TypeCode::utf8:
			case TypeCode::array:
			case TypeCode::map:
			case TypeCode::tag:
			case TypeCode::simple:
			case TypeCode::error:
				return (int64_t)additional;
			case TypeCode::integerN:
				return (uint64_t)-1 - (uint64_t)additional;
				return additional;
			case TypeCode::float32:
				return (uint64_t)float32;
			case TypeCode::float64:
				return (uint64_t)float64;
			default:
				return 0;
		}
	}
	operator int64_t() const {
		switch (typeCode) {
			case TypeCode::integerP:
			case TypeCode::bytes:
			case TypeCode::utf8:
			case TypeCode::array:
			case TypeCode::map:
			case TypeCode::tag:
			case TypeCode::simple:
			case TypeCode::error:
				return (int64_t)additional;
			case TypeCode::integerN:
				return -1 - (int64_t)additional;
			case TypeCode::float32:
				return (uint64_t)float32;
			case TypeCode::float64:
				return (uint64_t)float64;
			default:
				return 0;
		}
	}
	operator uint32_t() const {
		return (uint32_t)(uint64_t)(*this);
	}
	operator uint16_t() const {
		return (uint16_t)(uint64_t)(*this);
	}
	operator uint8_t() const {
		return (uint32_t)(uint64_t)(*this);
	}
	operator size_t() const {
		return (size_t)(uint64_t)(*this);
	}
	// For the signed ones, we cast from the signed 64-bit
	operator int32_t() const {
		return (int32_t)(int64_t)(*this);
	}
	operator int16_t() const {
		return (int16_t)(int64_t)(*this);
	}
	operator int8_t() const {
		return (int8_t)(int64_t)(*this);
	}
	
	bool isFloat() const {
		return typeCode == TypeCode::float32 || typeCode == TypeCode::float64;
	}
	operator double() const {
		switch (typeCode) {
			case TypeCode::float32:
				return float32;
			case TypeCode::float64:
				return float64;
			default:
				return 0;
		}
	}
	operator float() const {
		return (float)(double)(*this);
	}
	
	bool isBytes() const {
		return typeCode == TypeCode::bytes || typeCode == TypeCode::indefiniteBytes;
	}
	bool isUtf8() const {
		return typeCode == TypeCode::utf8 || typeCode == TypeCode::indefiniteUtf8;
	}
	bool hasLength() const {
		return typeCode != TypeCode::indefiniteBytes && typeCode != TypeCode::indefiniteUtf8 && typeCode != TypeCode::indefiniteArray && typeCode != TypeCode::indefiniteMap;
	}
	size_t length() const {
		return (size_t)(*this);
	}
	
	const unsigned char * bytes() const {
		return dataNext;
	}

	std::string utf8() const {
		return {(const char *)dataNext, length()};
	}
#ifdef CBOR_WALKER_USE_STRING_VIEW
	std::string_view utf8View() const {
		return {(const char *)dataNext, length()};
	}
#endif

	bool isArray() const {
		return typeCode == TypeCode::array || typeCode == TypeCode::indefiniteArray;
	}
	template<class Fn>
	CborWalker forEach(Fn &&fn, bool mapValues=true) const {
		if (typeCode == TypeCode::array) {
			size_t count = length();
			CborWalker item = enter();
			for (size_t i = 0; i < count; ++i) {
				if (item.error()) return item;
				fn(item, i);
				item = item.next();
			}
			return item;
		} else if (typeCode == TypeCode::indefiniteArray) {
			CborWalker item = enter();
			size_t i = 0;
			while (!item.error() && !item.isExit()) {
				fn(item, i);
				item = item.next();
				++i;
			}
			return item.next(); // move past the exit
		} else if (typeCode == TypeCode::indefiniteBytes) {
			CborWalker item = enter();
			size_t i = 0;
			while (!item.error() && !item.isExit()) {
				if (item.typeCode != TypeCode::bytes) return {data, dataEnd, ERROR_INVALID_VALUE};
				fn(item, i);
				item = item.next();
				++i;
			}
			return item.next(); // move past the exit
		} else if (typeCode == TypeCode::indefiniteUtf8) {
			CborWalker item = enter();
			size_t i = 0;
			while (!item.error() && !item.isExit()) {
				if (item.typeCode != TypeCode::utf8) return {data, dataEnd, ERROR_INVALID_VALUE};
				fn(item, i);
				item = item.next();
				++i;
			}
			return item.next(); // move past the exit
		} else if (typeCode == TypeCode::map) {
			size_t count = length();
			CborWalker item = enter();
			for (size_t i = 0; i < count; ++i) {
				if (item.error()) return item;
				if (!mapValues) fn(item, i);
				item = item.next();
				if (item.error()) return item;
				if (mapValues) fn(item, i);
				item = item.next();
			}
			return item;
		} else if (typeCode == TypeCode::indefiniteMap) {
			CborWalker item = enter();
			size_t i = 0;
			while (!item.error() && !item.isExit()) {
				if (!mapValues) fn(item, i);
				item = item.next();
				if (item.error()) return item;
				if (item.isExit()) return {item.data, item.dataEnd, ERROR_INVALID_VALUE};
				if (mapValues) fn(item, i);
				item = item.next();
				++i;
			}
			return item.next(); // move past the exit
		}
		return {data, dataEnd, ERROR_METHOD_TYPE_MISMATCH};
	}
	
	bool isMap() const {
		return typeCode == TypeCode::map || typeCode == TypeCode::indefiniteMap;
	}

	template<class Fn>
	CborWalker forEachPair(Fn &&fn) const {
		if (typeCode == TypeCode::map) {
			size_t count = length();
			CborWalker item = enter();
			for (size_t i = 0; i < count; ++i) {
				auto key = item;
				item = item.next();
				if (key.error() || item.error()) return item;
				fn(key, item);
				item = item.next();
			}
			return item;
		} else if (typeCode == TypeCode::indefiniteMap) {
			CborWalker item = enter();
			while (!item.error() && !item.isExit()) {
				auto key = item;
				item = item.next();
				if (key.error() || item.error()) return item;
				if (item.isExit()) return {item.data, item.dataEnd, ERROR_INVALID_VALUE};
				fn(key, item);
				item = item.next();
			}
			return item.next(); // move past the exit
		}
		return {data, dataEnd, ERROR_METHOD_TYPE_MISMATCH};
	}
	
	bool isEnd() const {
		return typeCode == TypeCode::array || typeCode == TypeCode::indefiniteArray;
	}

	bool isTagged() const {
		return typeCode == TypeCode::tag;
	}
	
//protected:
	CborWalker(const unsigned char *data, const unsigned char *dataEnd, uint64_t errorCode) : data(data), dataEnd(dataEnd), dataNext(nullptr), typeCode(TypeCode::error), additional(errorCode) {}

	// The next *core* value - but doesn't check whether the current value is the header for a string/array/etc.
	CborWalker nextBasic() const {
		return {dataNext, dataEnd};
	}

	const unsigned char *data, *dataEnd, *dataNext;
	enum class TypeCode {
		integerP, integerN, bytes, utf8, array, map, tag, simple, float32, float64,
		error, indefiniteBreak, indefiniteBytes, indefiniteUtf8, indefiniteArray, indefiniteMap
	};
	TypeCode typeCode;
	union {
		uint64_t additional;
		float float32;
		double float64;
		unsigned char additionalBytes[8];
	};
};

// Automatically skips over tags, but still lets you query them
struct TaggedCborWalker : public CborWalker {
	TaggedCborWalker() {}
	TaggedCborWalker(const CborWalker& basic) : CborWalker(basic), tagStart(basic.data) {
		consumeTags();
	}
	TaggedCborWalker(const unsigned char *dataStart, const unsigned char *dataEnd) : CborWalker(dataStart, dataEnd), tagStart(data) {
		consumeTags();
	}
	
	TaggedCborWalker next() const {
		return CborWalker::next();
	}
	TaggedCborWalker enter() const {
		return CborWalker::enter();
	}
	TaggedCborWalker nextExit() const {
		return CborWalker::nextExit();
	}
	template<class Fn>
	TaggedCborWalker forEach(Fn &&fn) const {
		return CborWalker::forEach([&](CborWalker &item, size_t i){
			fn(TaggedCborWalker{item}, i);
		});
	}
	template<class Fn>
	TaggedCborWalker forEachPair(Fn &&fn) const {
		return CborWalker::forEachPair([&](CborWalker &key, CborWalker &value){
			fn(TaggedCborWalker{key}, TaggedCborWalker{value});
		});
	}
	
	size_t tagCount() const {
		return nTags;
	}
	
	uint64_t tag(size_t tagIndex) const {
		CborWalker tagWalker(tagStart, dataEnd);
		for (size_t i = 0; i < tagIndex; ++i) {
			tagWalker = tagWalker.enter();
		}
		return tagWalker;
	}
	
private:
	size_t nTags = 0;
	const unsigned char *tagStart;
	
	void consumeTags() {
		while (isTagged() && data < dataEnd) {
			++nTags;
			// Move "into" the tag
			CborWalker::operator=(enter());
		}
	}
};

}} // namespace

#endif // include guard
