#ifndef SIGNALSMITH_CBOR_WALKER_H
#define SIGNALSMITH_CBOR_WALKER_H

#include <cstdint>
#include <cmath>
#include <cstring>
#ifndef UINT64_MAX
#	define UINT64_MAX 0xFFFFFFFFFFFFFFFFull;
#endif

#include <vector>
#include <string>
#if __cplusplus >= 201703L
#	define CBOR_WALKER_USE_STRING_VIEW
#	include <string_view>
#endif
#if __cplusplus >= 202002L
#	define CBOR_WALKER_USE_BIT_CAST
#	include <bit>
#endif

namespace signalsmith { namespace cbor {

struct CborWalker {
	CborWalker(uint64_t errorCode=ERROR_NOT_INITIALISED) : CborWalker(nullptr, nullptr, errorCode) {}
	CborWalker(const std::vector<unsigned char> &vector) : CborWalker(vector.data(), vector.size()) {}
	CborWalker(const unsigned char *data, size_t length) : CborWalker(data, data + length) {}
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
#ifdef CBOR_WALKER_HALF_PRECISION_FLOAT
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
#else
				float32 = 0;
#endif
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
	
	// All error codes are non-zero, so can be checked with `.error()`
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
			++result;
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
				++result;
			}
			return result;
		}
		case TypeCode::map: {
			auto result = nextBasic();
			auto length = additional;
			for (uint64_t i = 0; i < length; ++i) {
				++result;
				++result;
			}
			return result;
		}
		case TypeCode::indefiniteBytes: {
			auto result = nextBasic();
			while (!result.error() && result.typeCode != TypeCode::indefiniteBreak) {
				if (result.typeCode != TypeCode::bytes) {
					return {data, dataEnd, ERROR_INCONSISTENT_INDEFINITE};
				}
				++result;
			}
			return result.nextBasic();
		}
		case TypeCode::indefiniteUtf8: {
			auto result = nextBasic();
			while (!result.error() && result.typeCode != TypeCode::indefiniteBreak) {
				if (result.typeCode != TypeCode::utf8) {
					return {data, dataEnd, ERROR_INCONSISTENT_INDEFINITE};
				}
				++result;
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
				++result;
				++result;
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

	// ++Prefix increments the position, and returns itself
	CborWalker & operator++() {
		*this = next();
		return *this;
	}

	// Postfix++ increments the position, but returns the old position
	CborWalker operator++(int) {
		CborWalker result = *this;
		*this = next();
		return result;
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
			++result;
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

	bool isNumber() const {
		return isFloat() || isInt();
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
			case TypeCode::integerP:
				return (uint64_t)(*this);
			case TypeCode::integerN:
				return (int64_t)(*this);
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
		if (typeCode != TypeCode::utf8) return "";
		return {(const char *)dataNext, length()};
	}
#ifdef CBOR_WALKER_USE_STRING_VIEW
	std::string_view utf8View() const {
		if (typeCode != TypeCode::utf8) return {nullptr, 0};
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
				CborWalker value = item++;
				fn(value, i);
			}
			return item;
		} else if (typeCode == TypeCode::indefiniteArray) {
			CborWalker item = enter();
			size_t i = 0;
			while (!item.error() && !item.isExit()) {
				fn(item++, i++);
			}
			return item.next(); // move past the exit
		} else if (typeCode == TypeCode::indefiniteBytes) {
			CborWalker item = enter();
			size_t i = 0;
			while (!item.error() && !item.isExit()) {
				if (item.typeCode != TypeCode::bytes) return {data, dataEnd, ERROR_INVALID_VALUE};
				fn(item++, i++);
			}
			return item.next(); // move past the exit
		} else if (typeCode == TypeCode::indefiniteUtf8) {
			CborWalker item = enter();
			size_t i = 0;
			while (!item.error() && !item.isExit()) {
				if (item.typeCode != TypeCode::utf8) return {data, dataEnd, ERROR_INVALID_VALUE};
				fn(item++, i++);
			}
			return item.next(); // move past the exit
		} else if (typeCode == TypeCode::map) {
			size_t count = length();
			CborWalker item = enter();
			for (size_t i = 0; i < count; ++i) {
				if (item.error()) return item;
				CborWalker key = item++;
				if (item.error()) return item;
				CborWalker value = item++;
				fn(mapValues ? value : key, i);
			}
			return item;
		} else if (typeCode == TypeCode::indefiniteMap) {
			CborWalker item = enter();
			size_t i = 0;
			while (!item.error() && !item.isExit()) {
				CborWalker key = item++;
				if (item.error()) return item;
				if (item.isExit()) return {item.data, item.dataEnd, ERROR_INVALID_VALUE};
				CborWalker value = item++;
				fn(mapValues ? value : key, i);
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
				auto key = item++;
				if (key.error() || item.error()) return item;
				auto value = item++;
				fn(key, value);
			}
			return item;
		} else if (typeCode == TypeCode::indefiniteMap) {
			CborWalker item = enter();
			while (!item.error() && !item.isExit()) {
				auto key = item++;
				if (key.error() || item.error()) return item;
				if (item.isExit()) return {item.data, item.dataEnd, ERROR_INVALID_VALUE};
				auto value = item++;
				fn(CborWalker(key), CborWalker(value));
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
	
protected:
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
	TaggedCborWalker(const CborWalker& basic) : CborWalker(basic), tagStart(data) {
		consumeTags();
	}
	TaggedCborWalker(const unsigned char *dataStart, const unsigned char *dataEnd) : CborWalker(dataStart, dataEnd), tagStart(dataStart) {
		consumeTags();
	}
	
	TaggedCborWalker next(size_t i=1) const {
		return CborWalker::next(i);
	}
	TaggedCborWalker & operator++() {
		CborWalker::operator++();
		return *this;
	}
	TaggedCborWalker operator++(int _) {
		return CborWalker::operator++(_);
	}
	TaggedCborWalker enter() const {
		return CborWalker::enter();
	}
	TaggedCborWalker nextExit() const {
		return CborWalker::nextExit();
	}
	template<class Fn>
	TaggedCborWalker forEach(Fn &&fn) const {
		return CborWalker::forEach([&](const CborWalker &item, size_t i){
			fn(TaggedCborWalker{item}, i);
		});
	}
	template<class Fn>
	TaggedCborWalker forEachPair(Fn &&fn) const {
		return CborWalker::forEachPair([&](const CborWalker &key, const CborWalker &value){
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
	
	bool isTypedArray() const {
		return isBytes() && typedArrayTag;
	}
	
	size_t typedArrayLength() const {
		uint8_t widthLog2 = typedArrayTag&0x03;
		uint8_t elementType = (typedArrayTag&0x18)>>3; // unsigned, signed, float
		widthLog2 += (elementType == 2); // int sizes are 8-64 bits, float sizes are 16-128
		size_t stride = 1<<widthLog2;
		return length()/stride;
	}
	
	template<class Array>
	size_t readTypedArray(Array &&array) const {
		return readTypedArray(array, 0, typedArrayLength());
	}

	template<class Array>
	size_t readTypedArray(Array &&array, size_t offset, size_t maxCount) const {
		size_t byteLength = length();
		
		bool bigEndian = !(typedArrayTag&0x04);
		
		switch (typedArrayTag&0xFB) { // without endian flag
		// unsigned int
		case 64: {
			size_t count = std::min(maxCount, byteLength);
			const uint8_t *bytes = dataNext + offset;
			for (size_t i = 0; i < count; ++i) {
				array[i] = bytes[i];
			}
			return count;
		}
		case 65:
			return typedArrayReadInner<Array, uint16_t, uint16_t>(array, offset, maxCount, bigEndian);
		case 66:
			return typedArrayReadInner<Array, uint32_t, uint32_t>(array, offset, maxCount, bigEndian);
		case 67:
			return typedArrayReadInner<Array, uint64_t, uint64_t>(array, offset, maxCount, bigEndian);
		// signed int
		case 72: {
			size_t count = std::min(maxCount, byteLength);
			const uint8_t *bytes = dataNext + offset;
			for (size_t i = 0; i < count; ++i) {
				array[i] = (int8_t)bytes[i]; // cast to signed here first, to make sure negatives behave correctly
			}
			return count;
		}
		case 73:
			return typedArrayReadInner<Array, uint16_t, int16_t>(array, offset, maxCount, bigEndian);
		case 74:
			return typedArrayReadInner<Array, uint32_t, int32_t>(array, offset, maxCount, bigEndian);
		case 75:
			return typedArrayReadInner<Array, uint64_t, int64_t>(array, offset, maxCount, bigEndian);
		// floating-point
		case 80:
			// TODO: half-precision float support
			return 0;
		case 81:
			return typedArrayReadInner<Array, uint32_t, float, true>(array, offset, maxCount, bigEndian);
		case 82:
			return typedArrayReadInner<Array, uint64_t, double, true>(array, offset, maxCount, bigEndian);
		case 83:
			// TODO: quad-precision float support
			return 0;
		default:
			return 0;
		}
	}

private:
	size_t nTags = 0;
	const unsigned char *tagStart;
	
	uint8_t typedArrayTag = 0;
	
	void consumeTags() {
		while (isTagged() && data < dataEnd) {
			++nTags;
			uint64_t tag = (*this);
			if (tag >= 64 && tag < 87) { // RFC-8746 range
				typedArrayTag = tag;
			}
			// Move "into" the tag
			CborWalker::operator=(enter());
		}
	}
	
	template<class Array, typename UIntType, typename ResultT, bool bitcast=false>
	size_t typedArrayReadInner(Array &&array, size_t offset, size_t maxCount, bool bigEndian) const {
		constexpr size_t B = sizeof(UIntType);
		if (offset*B > length()) return 0;
		const uint8_t *bytes = dataNext + offset*B;
		size_t count = std::min(maxCount, length()/B - offset);
		if (bigEndian) {
			for (size_t i = 0; i < count; ++i) {
				UIntType v = 0;
				for (size_t b = 0; b < B; ++b) {
					UIntType bv = bytes[i*B + b];
					v += bv<<((B - 1 - b)*8);
				}
				if (bitcast) {
#ifdef CBOR_WALKER_USE_BIT_CAST
					array[i] = std::bit_cast<ResultT>(v);
#else
					ResultT r;
					std::memcpy(&r, &v, B);
					array[i] = r;
#endif
				} else {
					array[i] = (ResultT)v;
				}
			}
		} else {
			for (size_t i = 0; i < count; ++i) {
				UIntType v = 0;
				for (size_t b = 0; b < B; ++b) {
					UIntType bv = bytes[i*B + b];
					v += bv<<(b*8);
				}
				if (bitcast) {
#ifdef CBOR_WALKER_USE_BIT_CAST
					array[i] = std::bit_cast<ResultT>(v);
#else
					ResultT r;
					std::memcpy(&r, &v, B);
					array[i] = r;
#endif
				} else {
					array[i] = (ResultT)v;
				}
			}
		}
		return count;
	}
};

template<class SubClassCRTP>
struct CborWriterBase {
	void addUInt(uint64_t u) {
		writeHead(0, u);
	}
	void addInt(int64_t u) {
		if (u >= 0) {
			writeHead(0, u);
		} else {
			writeHead(1, -1 - u);
		}
	}
	void addTag(uint64_t u) {
		writeHead(6, u);
	}
	void addBool(bool b) {
		writeHead(7, 20 + b);
	}
	void openArray() {
		sub().writeByte(0x9F);
	}
	void openArray(size_t items) {
		writeHead(4, items);
	}
	void openMap() {
		sub().writeByte(0xBF);
	}
	void openMap(size_t pairs) {
		writeHead(5, pairs);
	}
	void close() {
		sub().writeByte(0xFF);
	}
	void addBytes(const void *ptr, size_t length) {
		addBytes((const unsigned char *)ptr, length);
	}
	void addBytes(const unsigned char *ptr, size_t length) {
		writeHead(2, length);
		sub().writeBytes(ptr, length);
	}
	void openBytes() {
		sub().writeByte(0x5F);
	}
	void addUtf8(const char *ptr, size_t length) {
		writeHead(3, length);
		sub().writeBytes((const unsigned char *)ptr, length);
	}
	void addUtf8(const char *str) {
		addUtf8(str, std::strlen(str));
	}
	void addUtf8(const std::string &str) {
		addUtf8(str.c_str());
	}
#ifdef CBOR_WALKER_USE_STRING_VIEW
	void addUtf8(const std::string_view &str) {
		addUtf8(str.data(), str.size());
	}
#endif
	void openUtf8() {
		sub().writeByte(0x7F);
	}
	void addNull() {
		sub().writeByte(0xF6);
	}
	void addUndefined() {
		sub().writeByte(0xF7);
	}
	void addSimple(unsigned char k) {
		writeHead(7, k);
	}
	
	void addFloat(float v) {
		sub().writeByte(0xFA);
#ifdef CBOR_WALKER_USE_BIT_CAST
		uint32_t vi = std::bit_cast<uint32_t>(v);
#else
		uint32_t vi;
		std::memcpy(&vi, &v, 4);
#endif
		for (size_t i = 0; i < 4; ++i) {
			auto shift = (3 - i)*8;
			sub().writeByte((vi>>shift)&0xFF);
		}
	}
	void addFloat(double v) {
		sub().writeByte(0xFB);
#ifdef CBOR_WALKER_USE_BIT_CAST
		uint64_t vi = std::bit_cast<uint64_t>(v);
#else
		uint64_t vi;
		std::memcpy(&vi, &v, 8);
#endif
		for (size_t i = 0; i < 8; ++i) {
			auto shift = (7 - i)*8;
			sub().writeByte((vi>>shift)&0xFF);
		}
	}
	
	// RFC-8746 tags for typed arrays
	// bits: [1, 0] = log2(elementBytes),  [2] = isLittleEndian, [3, 4] = [unsigned, signed, float]
	void addTypedArray(const uint8_t *arr, size_t length) {
		addTag(64);
		addBytes((const void *)arr, length);
	}
	void addTypedArray(const int8_t *arr, size_t length) {
		addTag(72);
		addBytes((const void *)arr, length);
	}
	void addTypedArray(const uint16_t *arr, size_t length, bool bigEndian=false) {
		addTag(bigEndian ? 65 : 69);
		writeTypedBlock<uint16_t>(arr, length, bigEndian);
	}
	void addTypedArray(const uint32_t *arr, size_t length, bool bigEndian=false) {
		addTag(bigEndian ? 66 : 70);
		writeTypedBlock<uint32_t>(arr, length, bigEndian);
	}
	void addTypedArray(const uint64_t *arr, size_t length, bool bigEndian=false) {
		addTag(bigEndian ? 67 : 71);
		writeTypedBlock<uint64_t>(arr, length, bigEndian);
	}
	// For signed ints, we make a proxy struct which casts them on-the-fly
	void addTypedArray(const int16_t *arr, size_t length, bool bigEndian=false) {
		addTag(bigEndian ? 73 : 77);
		struct {
			const int16_t *arr;
			uint16_t operator[](size_t i) const {
				return (uint16_t)(arr[i]);
			}
		} unsignedArray{arr};
		writeTypedBlock<uint16_t>(unsignedArray, length, bigEndian);
	}
	void addTypedArray(const int32_t *arr, size_t length, bool bigEndian=false) {
		addTag(bigEndian ? 74 : 78);
		struct {
			const int32_t *arr;
			uint32_t operator[](size_t i) const {
				return (uint32_t)(arr[i]);
			}
		} unsignedArray{arr};
		writeTypedBlock<uint32_t>(unsignedArray, length, bigEndian);
	}
	void addTypedArray(const int64_t *arr, size_t length, bool bigEndian=false) {
		addTag(bigEndian ? 75 : 79);
		struct {
			const int64_t *arr;
			uint64_t operator[](size_t i) const {
				return (uint64_t)(arr[i]);
			}
		} unsignedArray{arr};
		writeTypedBlock<uint64_t>(unsignedArray, length, bigEndian);
	}
	// Look, I'm not any happier about this than you are
	void addTypedArray(const float *arr, size_t length, bool bigEndian=false) {
		addTag(bigEndian ? 81 : 85);
		struct {
			const float *arr;
			uint32_t operator[](size_t i) const {
#ifdef CBOR_WALKER_USE_BIT_CAST
				return std::bit_cast<uint32_t>(arr[i]);
#else
				float v = arr[i];
				uint32_t vi;
				std::memcpy(&vi, &v, 4);
				return vi;
#endif
			}
		} unsignedArray{arr};
		writeTypedBlock<uint32_t>(unsignedArray, length, bigEndian);
	}
	void addTypedArray(const double *arr, size_t length, bool bigEndian=false) {
		addTag(bigEndian ? 82 : 86);
		struct {
			const double *arr;
			uint64_t operator[](size_t i) const {
#ifdef CBOR_WALKER_USE_BIT_CAST
				return std::bit_cast<uint64_t>(arr[i]);
#else
				double v = arr[i];
				uint64_t vi;
				std::memcpy(&vi, &v, 8);
				return vi;
#endif
			}
		} unsignedArray{arr};
		writeTypedBlock<uint64_t>(unsignedArray, length, bigEndian);
	}
private:
	SubClassCRTP & sub() {
		return *(SubClassCRTP *)this;
	}

	void writeHead(unsigned char type, uint64_t argument) {
		type <<= 5;
		if (argument >= 4294967296ul) {
			sub().writeByte(type|27);
			for (size_t i = 0; i < 8; ++i) {
				sub().writeByte(argument>>(56 - i*8));
			}
		} else if (argument >= 65536) {
			sub().writeByte(type|26);
			for (size_t i = 0; i < 4; ++i) {
				sub().writeByte(argument>>(24 - i*8));
			}
		} else if (argument >= 256) {
			sub().writeByte(type|25);
			sub().writeByte(argument>>8);
			sub().writeByte(argument);
		} else if (argument >= 24) {
			sub().writeByte(type|24);
			sub().writeByte(argument);
		} else {
			sub().writeByte(type|argument);
		}
	}
	
	template<typename UIntType, class Array>
	void writeTypedBlock(Array &&array, size_t length, bool bigEndian) {
		constexpr size_t B = sizeof(UIntType);
		writeHead(2, length*B);
		if (bigEndian) {
			for (size_t i = 0; i < length; ++i) {
				UIntType v = array[i];
				for (size_t b = 0; b < B; ++b) sub().writeByte((v>>((B-1-b)*8))&0xFF);
			}
		} else {
			for (size_t i = 0; i < length; ++i) {
				UIntType v = array[i];
				for (size_t b = 0; b < B; ++b) sub().writeByte((v>>(b*8))&0xFF);
			}
		}
	}
};

struct CborWriter : public CborWriterBase<CborWriter> {
	CborWriter(std::vector<unsigned char> &bytes) : bytes(bytes) {}
	
private:
	friend struct CborWriterBase<CborWriter>;

	std::vector<unsigned char> &bytes;
	void writeByte(unsigned char b) {
		bytes.push_back(b);
	}
	void writeBytes(const unsigned char *ptr, size_t length) {
		bytes.insert(bytes.end(), ptr, ptr + length);
	}
};

struct CborWriterStream : public CborWriterBase<CborWriterStream> {
	CborWriterStream(std::ostream &output) : output(output) {}
	
private:
	friend struct CborWriterBase<CborWriterStream>;

	std::ostream &output;
	void writeByte(unsigned char b) {
		output.put((char)b);
	}
	void writeBytes(const unsigned char *ptr, size_t length) {
		output.write((const char *)ptr, length);
	}
};

}} // namespace

#endif // include guard
