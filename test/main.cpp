#include <iostream>
#define LOG_EXPR(expr) std::cout << #expr << " = " << (expr) << std::endl;

#include "../cbor-walker.h"

#include <string>
#include <sstream>

int main() {
	std::vector<unsigned char> bytes;
	
	signalsmith::cbor::CborWalker cbor;
	signalsmith::cbor::TaggedCborWalker taggedCbor;

	auto decode4Bits = [](char c) {
		if (c >= 'a') {
			return 10 + (c - 'a');
		} else if (c >= 'A') {
			return 10 + (c - 'A');
		} else {
			return c - '0';
		}
	};
	auto decodeHex = [&](const char *str){
		std::cout << "Hex: " << str << std::endl;
		if (str[0] == '0' && str[1] == 'x') str += 2;
		
		bytes.resize(0);
		size_t length = std::strlen(str);
		for (size_t i = 0; i < length; i += 2) {
			char high = decode4Bits(str[i]), low = decode4Bits(str[i + 1]);
			bytes.push_back((high<<4)|low);
		}
		cbor = {bytes.data(), bytes.data() + bytes.size()};
		taggedCbor = {bytes.data(), bytes.data() + bytes.size()};
	};
	auto test = [&](bool condition, const std::string &reason){
		if (condition) {
			std::cout << "\t-\t" << reason << std::endl;
		} else {
			std::cout << "\tFAILED\t" << reason << std::endl;
			std::exit(1);
		}
	};
	const char *typeNames[] = {"integerP", "integerN", "bytes", "utf8", "array", "map", "tag", "simple", "float32", "float64", "error", "indefiniteBreak", "indefiniteBytes", "indefiniteUtf8", "indefiniteArray", "indefiniteMap"};
	auto logState = [&]() {
		std::cout <<"\t@ " << (int)(cbor.data - bytes.data()) << "\t" << typeNames[(int)cbor.typeCode] << " : " << cbor.additional << " [";
		for (int i = 0; i < 8; ++i) std::cout << " " << (int)cbor.additionalBytes[i];
		std::cout << " ]\n";
	};
	(void)logState;
	
	// Examples from RFC 8949 Appendix A
	auto testInt = [&](int64_t v, const char *hex) {
		decodeHex(hex);
		test(cbor.isInt(), "isInt()");
		test((int64_t)cbor == v, std::to_string((int64_t)cbor) + " == " + std::to_string(v));
	};
	auto testUInt = [&](uint64_t v, const char *hex) {
		decodeHex(hex);
		test(cbor.isInt(), "isInt()");
		test((uint64_t)cbor == v, std::to_string((uint64_t)cbor) + " == " + std::to_string(v));
	};
	testInt(0, "0x00");
	testInt(1, "0x01");
	testInt(10, "0x0a");
	testInt(23, "0x17");
	testInt(24, "0x1818");
	testInt(25, "0x1819");
	testInt(100, "0x1864");
	testInt(1000, "0x1903e8");
	testInt(1000000, "0x1a000f4240");
	testUInt(1000000000000, "0x1b000000e8d4a51000");
	testUInt(18446744073709551615ul, "0x1bffffffffffffffff");
	// We don't support big enough numbers:
	// -18446744073709551616	0x3bffffffffffffffff
	// -18446744073709551617	0xc349010000000000000000
	testInt(-1, "0x20");
	testInt(-10, "0x29");
	testInt(-100, "0x3863");
	testInt(-1000, "0x3903e7");
	
	auto testFloat = [&](double v, const char *hex, bool wiggleRoom=false) {
		decodeHex(hex);
		logState();
		test(cbor.isFloat(), "isFloat()");
		std::stringstream ss;
		ss << std::scientific;
		ss << (double)cbor << " == " << v;
		if (wiggleRoom) {
			double diff = v - (double)cbor;
			double ratio = diff/v;
			std::cout << "diff = " << diff << ", ratio = " << ratio << "\n";
		} else {
			test((double)cbor == v, ss.str());
		}
	};
	testFloat(0.0, "0xf90000");
	testFloat(-0.0, "0xf98000");
	testFloat(1.1, "0xfb3ff199999999999a");
	testFloat(1.5, "0xf93e00");
	testFloat(100000.0, "0xfa47c35000");
	testFloat(3.4028234663852886e+38, "0xfa7f7fffff");
	testFloat(1.0e+300, "0xfb7e37e43c8800759c");
	testFloat(5.960464477539063e-8, "0xf90001", false);
	testFloat(0.00006103515625, "0xf90400");
	testFloat(-4.0, "0xf9c400");
	testFloat(-4.1, "0xfbc010666666666666");

	testFloat(INFINITY, "0xf97c00");
	decodeHex("0xf97e00");
	test(std::isnan((double)cbor), "NaN double");
	test(std::isnan((float)cbor), "NaN float");
	testFloat(-INFINITY, "0xf9fc00");

	testFloat(INFINITY, "0xfa7f800000");
	decodeHex("0xfa7fc00000");
	test(std::isnan((double)cbor), "NaN double");
	test(std::isnan((float)cbor), "NaN float");
	testFloat(-INFINITY, "0xfaff800000");

	testFloat(INFINITY, "0xfb7ff0000000000000");
	decodeHex("0xfb7ff8000000000000");
	test(std::isnan((double)cbor), "NaN double");
	test(std::isnan((float)cbor), "NaN float");
	testFloat(-INFINITY, "0xfbfff0000000000000");
	
	//-----
	
	decodeHex("0xf4");
	test(cbor.isBool(), "is bool");
	test((bool)cbor == false, "is false");

	decodeHex("0xf5");
	test(cbor.isBool(), "is bool");
	test((bool)cbor == true, "is true");

	decodeHex("0xf6");
	test(cbor.isNull(), "is null");

	decodeHex("0xf7");
	test(cbor.isUndefined(), "is undefined");

	decodeHex("0xf0");
	test(cbor.isSimple(), "is simple");
	test((int)cbor == 16, "simple value == 16");

	decodeHex("0xf8ff");
	test(cbor.isSimple(), "is simple");
	test((int)cbor == 255, "simple value == 255");
	
	test(taggedCbor.tagCount() == 0, "no tags");
	
	// Tags
	
	decodeHex("0xc074323031332d30332d32315432303a30343a30305a");
	test(cbor.isTagged(), "is tagged");
	test((int)cbor == 0, "tag == 0");
	cbor = cbor.enter();
	test(cbor.isUtf8(), "is string");
	test(cbor.hasLength(), "has a (defined) length");
	
	test(taggedCbor.isUtf8(), "tagged is UTF8 already");
	test(taggedCbor.tagCount() == 1, "and has one tag");
	test(taggedCbor.tag(0) == 0, "tag(0) == 0");

	decodeHex("0xc11a514b67b0");
	test(cbor.isTagged(), "is tag");
	test((int)cbor == 1, "tag == 1");
	cbor = cbor.enter();
	test(cbor.isInt(), "value is int");
	test((int)cbor == 1363896240, "value == 1363896240");
	
	test(taggedCbor.isInt(), "tagged is UTF8 already");
	test(taggedCbor.tagCount() == 1, "and has one tag");
	test(taggedCbor.tag(0) == 1, "tag(0) == 0");

	decodeHex("0xc1fb41d452d9ec200000");
	test(cbor.isTagged(), "is tagged");
	test((int)cbor == 1, "tag == 1");
	cbor = cbor.enter();
	test(cbor.isFloat(), "value is float");
	test((double)cbor == 1363896240.5, "value == 1363896240.5");
	
	test(taggedCbor.isFloat(), "tagged is float already");
	test(taggedCbor.tagCount() == 1, "and has one tag");
	test(taggedCbor.tag(0) == 1, "tag(0) == 0");

	decodeHex("0xd74401020304");
	test(cbor.isTagged(), "is tag");
	test((int)cbor == 23, "tag == 23");
	{
		auto nextCbor = cbor.enter();
		test(nextCbor.isBytes(), "value is bytes");
		test(nextCbor.length() == 4, "length() == 4");
		test(nextCbor.bytes()[0] == 1, "bytes[0] == 1");
		test(nextCbor.bytes()[2] == 3, "bytes[2] == 3");
	}
	test(cbor.isTagged(), "still tagged");
	cbor = cbor.next();
	test(cbor.error() == signalsmith::cbor::CborWalker::ERROR_END_OF_DATA, "reached end of input");
	
	test(taggedCbor.isBytes(), "tagged isBytes already");
	test(taggedCbor.tagCount() == 1, "and has one tag");
	test(taggedCbor.tag(0) == 23, "tag(0) == 23");

	decodeHex("0xd818456449455446");
	test(cbor.isTagged(), "is tagged");
	test((int)cbor == 24, "tag == 24");
	cbor = cbor.enter();
	test(cbor.isBytes(), "value is bytes");
	test(cbor.length() == 5, "length() == 5");
	test(cbor.bytes()[0] == 0x64, "bytes[0] == 0x64");
	test(cbor.bytes()[2] == 0x45, "bytes[2] == 0x45");
	
	test(taggedCbor.isBytes(), "tagged isBytes already");
	test(taggedCbor.tagCount() == 1, "and has one tag");
	test(taggedCbor.tag(0) == 24, "tag(0) == 24");
	
	decodeHex("0xd82076687474703a2f2f7777772e6578616d706c652e636f6d");
	test(cbor.isTagged(), "is tag");
	test((int)cbor == 32, "tag == 32");
	cbor = cbor.enter();
	test(cbor.isUtf8(), "value is UTF8");
	test(cbor.length() == 22, "length() == 22");
	std::string str = cbor.utf8(); // it should return a string_view, which gets converted
	test(str == "http://www.example.com", "http://www.example.com");
	
	test(taggedCbor.isUtf8(), "tagged isUtf8 already");
	test(taggedCbor.tagCount() == 1, "and has one tag");
	test(taggedCbor.tag(0) == 32, "tag(0) == 32");
	
	// Bytes
	decodeHex("0x40");
	test(cbor.isBytes(), "is bytes");
	test(cbor.length() == 0, "length == 0");

	decodeHex("0x4401020304");
	test(cbor.isBytes(), "is bytes");
	test(cbor.length() == 4, "length == 4");
	test(cbor.bytes()[0] == 1, "bytes[0] == 1");
	test(cbor.bytes()[2] == 3, "bytes[2] == 3");

	// Strings
	decodeHex("0x60");
	test(cbor.isUtf8(), "is UTF8");
	test(cbor.length() == 0, "length == 0");

	decodeHex("0x6161");
	test(cbor.isUtf8(), "is UTF8");
	test(cbor.utf8() == "a", "string match");

	decodeHex("0x6449455446");
	test(cbor.isUtf8(), "is UTF8");
	test(cbor.utf8() == "IETF", "string match");

	decodeHex("0x62225c");
	test(cbor.isUtf8(), "is UTF8");
	test(cbor.utf8() == "\"\\", "string match");

	decodeHex("0x62c3bc");
	test(cbor.isUtf8(), "is UTF8");
	test(cbor.length() == 2, "length is 2 (even though it's a single UTF8 character)");
	test(cbor.bytes()[0] == 195, "byte 0");
	test(cbor.bytes()[1] == 188, "byte 1");

	decodeHex("0x63e6b0b4");
	test(cbor.isUtf8(), "is UTF8");
	test(cbor.length() == 3, "length is 3 (even though it's a single UTF8 character)");
	test(cbor.bytes()[0] == 230, "byte 0");
	test(cbor.bytes()[1] == 176, "byte 1");
	test(cbor.bytes()[2] == 180, "byte 2");

	decodeHex("0x64f0908591");
	test(cbor.isUtf8(), "is UTF8");
	test(cbor.length() == 4, "length is 4 (even though it's 2 UTF8 characters)");
	test(cbor.bytes()[0] == 240, "byte 0");
	test(cbor.bytes()[1] == 144, "byte 1");
	test(cbor.bytes()[2] == 133, "byte 2");
	test(cbor.bytes()[3] == 145, "byte 3");
	
	// Arrays!
	decodeHex("0x80");
	test(cbor.isArray(), "is array");
	test(cbor.hasLength(), "has a length");
	test(cbor.length() == 0, "length is 0");

	decodeHex("0x83010203");
	test(cbor.isArray(), "is array");
	test(cbor.hasLength(), "has a length");
	test(cbor.length() == 3, "length is 3");
	auto item = cbor.enter();
	test(item.isInt(), "item is int");
	test((int)item == 1, "item == 1");
	item = item.next();
	test(item.isInt(), "item is int");
	test((int)item == 2, "item == 2");
	item = item.next();
	test(item.isInt(), "item is int");
	test((int)item == 3, "item == 3");
	item = item.next();
	test(item.error() == signalsmith::cbor::CborWalker::ERROR_END_OF_DATA, "end of data");
	test(cbor.isArray(), "original is still array");

	decodeHex("0x8301820203820405");
	test(cbor.isArray(), "is array");
	test(cbor.enter().isInt() && (int)cbor.enter() == 1, "[1, ...]");
	test(cbor.enter().next().isArray(), "[1, [...], ...]");
	test(cbor.enter().next().enter().isInt(), "[1, [#, ...], ...]");
	test(cbor.enter().next(2).isArray(), "[1, [...], [...]");
	test(cbor.enter().next(2).enter().isInt(), "[1, [...], [#...]");
	test((int)cbor.enter().next(2).enter() == 4, "[1, [...], [4...]");
	test((int)cbor.enter().next(2).length() == 2, "[1, [...], [. .]");

	decodeHex("0x98190102030405060708090a0b0c0d0e0f101112131415161718181819");
	test(cbor.isArray(), "is array");
	test(cbor.length() == 25, "length 25");
	for (size_t i = 0; i < 25; ++i) {
		auto item = cbor.enter().next(i);
		test(item.isInt() && (size_t)item == (i + 1), "item #" + std::to_string(i));
	}
	{
		auto next = cbor.forEach([&](auto item, size_t i){
			test(item.isInt() && (size_t)item == (i + 1), "forEach #" + std::to_string(i));
		});
		test(next.error() == signalsmith::cbor::CborWalker::ERROR_END_OF_DATA, "returns next item");
	}

	// Map
	decodeHex("0xa0");
	test(cbor.isMap(), "is map");
	test(cbor.length() == 0, "length 0");

	decodeHex("0xa201020304");
	test(cbor.isMap(), "is map");
	test(cbor.length() == 2, "length 2");
	cbor.forEachPair([&](auto key, auto value){
		test(key.isInt(), "key is int");
		test(value.isInt(), "value is int");
		test((int)value == (int)key + 1, "value = key + 1");
	});

	decodeHex("0xa26161016162820203");
	test(cbor.isMap(), "is map");
	test(cbor.length() == 2, "length 2");
	test(cbor.enter().utf8() == "a", "first key: a");
	test((int)cbor.enter().next() == 1, "first value: 1");
	test(cbor.enter().next(2).utf8() == "b", "second key: b");
	test((int)cbor.enter().next(3).isArray(), "second value is an array");

	decodeHex("0x826161a161626163");
	test(cbor.isArray(), "is array");
	test(cbor.length() == 2, "length 2");
	test(cbor.enter().utf8() == "a", "first item: a");
	test(cbor.enter().next().isMap(), "second item is map");
	test(cbor.enter().next().enter().utf8() == "b", "second item's first key: b");

	decodeHex("0xa56161614161626142616361436164614461656145");
	test(cbor.isMap(), "is map");
	test(cbor.length() == 5, "length 5");
	cbor.forEachPair([&](auto key, auto value){
		test(key.isUtf8(), "key is string");
		test(value.isUtf8(), "value is string");
		char cKey = key.bytes()[0];
		char cValue = value.bytes()[0];
		test(cValue == cKey - 32, "value is capital of key (or starts with it)");
	});
	
	// Indefinite-length stuff
	decodeHex("0x5f42010243030405ff");
	test(cbor.isBytes(), "is bytes");
	test(!cbor.hasLength(), "but not defined length");
	{
		size_t totalLength = 0;
		auto next = cbor.forEach([&](auto v, size_t i){
			test(v.isBytes() && v.hasLength(), "item is bytes with defined length");
			totalLength += v.length();
			test(v.length() == (2 + i), "chunk length");
		});
		test(next.atEnd(), "reached end");
		test(totalLength == 5, "total length 5");
	}

	// Indefinite-length stuff
	decodeHex("0x7f657374726561646d696e67ff");
	test(cbor.isUtf8(), "is UTF8");
	test(!cbor.hasLength(), "but not defined length");
	{
		std::string total = "";
		auto next = cbor.forEach([&](auto v, size_t i){
			test(v.isUtf8() && v.hasLength(), "item is UTF8 with defined length");
			total += v.utf8();
			test(v.length() == (5 - i), "chunk length");
		});
		test(next.atEnd(), "reached end");
		test(total == "streaming", "total: streaming");
	}

	decodeHex("0x9fff");
	test(cbor.isArray(), "is array");
	test(!cbor.hasLength(), "unknown length");
	test(cbor.enter().isExit(), "no items - exits immediately");

	decodeHex("0x9f018202039f0405ffff");
	test(cbor.isArray(), "is array");
	test(cbor.enter().isInt() && (int)cbor.enter() == 1, "[1, ...]");
	test(cbor.enter().next().isArray(), "[1, [...], ...]");
	test(cbor.enter().next().enter().isInt(), "[1, [#, ...], ...]");
	test(cbor.enter().next(2).isArray(), "[1, [...], [...]");
	test(cbor.enter().next(2).enter().isInt(), "[1, [...], [#...]");
	test((int)cbor.enter().next(2).enter() == 4, "[1, [...], [4...]");
	test(cbor.enter().next(1).hasLength(), "[1, [_ ...], [. .]");
	test(!cbor.enter().next(2).hasLength(), "[1, [...], [_ . .]");

	decodeHex("0x9f01820203820405ff");
	test(cbor.isArray(), "is array");
	test(cbor.enter().isInt() && (int)cbor.enter() == 1, "[1, ...]");
	test(cbor.enter().next().isArray(), "[1, [...], ...]");
	test(cbor.enter().next().enter().isInt(), "[1, [#, ...], ...]");
	test(cbor.enter().next(2).isArray(), "[1, [...], [...]");
	test(cbor.enter().next(2).enter().isInt(), "[1, [...], [#...]");
	test((int)cbor.enter().next(2).enter() == 4, "[1, [...], [4...]");
	test(cbor.enter().next().hasLength(), "[1, [...], [_ . .]");
	test(cbor.enter().next(2).hasLength(), "[1, [...], [_ . .]");

	decodeHex("0x83018202039f0405ff");
	test(cbor.isArray(), "is array");
	test(cbor.enter().isInt() && (int)cbor.enter() == 1, "[1, ...]");
	test(cbor.enter().next().isArray(), "[1, [...], ...]");
	test(cbor.enter().next().enter().isInt(), "[1, [#, ...], ...]");
	test(cbor.enter().next(2).isArray(), "[1, [...], [...]");
	test(cbor.enter().next(2).enter().isInt(), "[1, [...], [#...]");
	test((int)cbor.enter().next(2).enter() == 4, "[1, [...], [4...]");
	test(cbor.enter().hasLength(), "[1, [...], [_ . .]");
	test(!cbor.enter().next(2).hasLength(), "[1, [...], [_ . .]");

	decodeHex("0x83019f0203ff820405");
	test(cbor.isArray(), "is array");
	test(cbor.enter().isInt() && (int)cbor.enter() == 1, "[1, ...]");
	test(cbor.enter().next().isArray(), "[1, [...], ...]");
	test(cbor.enter().next().enter().isInt(), "[1, [#, ...], ...]");
	test(cbor.enter().next(2).isArray(), "[1, [...], [...]");
	test(cbor.enter().next(2).enter().isInt(), "[1, [...], [#...]");
	test((int)cbor.enter().next(2).enter() == 4, "[1, [...], [4...]");
	test(!cbor.enter().next(1).hasLength(), "[1, [_ ...], [. .]");
	test(cbor.enter().next(2).hasLength(), "[1, [...], [_ . .]");

	decodeHex("0x9f0102030405060708090a0b0c0d0e0f101112131415161718181819ff");
	test(cbor.isArray(), "is array");
	test(!cbor.hasLength(), "no defined length");
	for (size_t i = 0; i < 25; ++i) {
		auto item = cbor.enter().next(i);
		test(item.isInt() && (size_t)item == (i + 1), "item #" + std::to_string(i));
	}
	{
		size_t counter = 0;
		auto next = cbor.forEach([&](auto item, size_t i){
			test(item.isInt() && (size_t)item == (i + 1), "forEach #" + std::to_string(i));
			test(i == counter, "i == counter");
			++counter;
		});
		test(next.error() == signalsmith::cbor::CborWalker::ERROR_END_OF_DATA, "returns next item");
	}

	decodeHex("0xbf61610161629f0203ffff");
	test(cbor.isMap(), "is map");
	test(!cbor.hasLength(), "no defined length");
	{
		size_t counter = 0;
		auto next = cbor.forEachPair([&](auto key, auto value) {
			if (counter == 0) {
				test(key.isUtf8() && value.isInt(), "key/value types");
			} else if (counter == 1) {
				test(key.isUtf8() && value.isArray(), "key/value types");
			} else {
				test(false, "too many items");
			}
			++counter;
		});
		test(next.atEnd(), "next.atEnd()");
	}
	{
		size_t counter = 0;
		auto next = cbor.forEach([&](auto value, size_t i) {
			if (counter == 0) {
				test(value.isInt(), "value type");
			} else if (counter == 1) {
				test(value.isArray(), "value type");
			} else {
				test(false, "too many items");
			}
			test(i == counter, "i == counter");
			++counter;
		});
		test(next.atEnd(), "next.atEnd()");
	}
	{
		size_t counter = 0;
		auto next = cbor.forEach([&](auto key, size_t i) {
			if (counter == 0) {
				test(key.isUtf8(), "key type");
			} else if (counter == 1) {
				test(key.isUtf8(), "key type");
			} else {
				test(false, "too many items");
			}
			test(i == counter, "i == counter");
			++counter;
		}, false);
		test(next.atEnd(), "next.atEnd()");
	}

	decodeHex("0x826161bf61626163ff");
	test(cbor.isArray(), "is array");
	test(cbor.length() == 2, "length 2");
	test(cbor.enter().utf8() == "a", "first item: a");
	test(cbor.enter().next().isMap(), "second item is map");
	test(!cbor.enter().next().hasLength(), "second item has undefined length");
	test(cbor.enter().next().enter().utf8() == "b", "second item's first key: b");

	decodeHex("0xbf6346756ef563416d7421ff");
	test(cbor.isMap(), "is map");
	test(!cbor.hasLength(), "has no length");
	{
		bool hadFun = false, hadAmt = false;
		cbor.forEachPair([&](auto key, auto value){
			if (key.utf8() == "Fun") {
				test(!hadFun, "!hadFun");
				hadFun = true;
				test(value.isBool() && (bool)value, "value == true");
			} else if (key.utf8() == "Amt") {
				test(!hadAmt, "!hadAmt");
				hadAmt = true;
				test(value.isInt() && (int)value == -2, "value == -2");
			} else {
				test(false, "unknown key");
			}
		});
		test(hadFun, "had key 1");
		test(hadAmt, "had key 2");
	}
	
	// Check with https://geraintluff.github.io/cbor-debug/ - surround with 0x9F / 0xFF so it shows the sequence, and also checks it's closed properly
	// It doesn't follow the floating-point ones at the end - those were copied from https://evanw.github.io/float-toy/
	decodeHex(
		"0x00D8401864F4F53903E79F4301020304FF824301020304A20443010203056466697665BF044201022463666976FF"
		// typed arrays (2 floats, little-endian then big-endian)
		"D855" "48DB0F49400050C347" "D851" "4840490FDB47C35000"
		// typed arrays (2 doubles, little-endian then big-endian)
		"D856" "50182D4454FB21094066666666666610C0" "D852" "50400921FB54442D18C010666666666666"
		// floats
		"FA40490FDBFB400921FB54442D18"
	);
	
	std::vector<unsigned char> writeBytes;
	signalsmith::cbor::CborWriter writer(writeBytes);
	writer.addInt(0);
	writer.addTag(64);
	writer.addInt(100);
	writer.addBool(false);
	writer.addBool(true);
	writer.addInt(-1000);
	writer.openArray();
	unsigned char writeChars[3] = {0x01, 0x02, 0x03};
	writer.addBytes(writeChars, 3);
	writer.addInt(4);
	writer.close();
	writer.openArray(2);
	writer.addBytes(writeChars, 3);
	writer.addInt(4);
	writer.openMap(2);
	writer.addInt(4);
	writer.addBytes(writeChars, 3);
	writer.addInt(5);
	const char *writeString = "five";
	writer.addUtf8(writeString);
	writer.openMap();
	writer.addInt(4);
	writer.addBytes(writeChars, 2);
	writer.addInt(-5);
	writer.addUtf8(writeString, 3);
	writer.close();
	//testFloat(100000.0, "0xfa47c35000");
	//testFloat(-4.1, "0xfbc010666666666666");
	float writeFloats[2] = {3.1415927f, 100000.0f};
	double writeDoubles[2] = {3.141592653589793, -4.1};
	writer.addTypedArray(writeFloats, 2);
	writer.addTypedArray(writeFloats, 2, true);
	writer.addTypedArray(writeDoubles, 2);
	writer.addTypedArray(writeDoubles, 2, true);
	writer.addFloat(3.1415927f);
	writer.addFloat(3.141592653589793);
	
	for (size_t i = 0; i < writeBytes.size(); ++i) {
		unsigned char byte = writeBytes[i];
		unsigned char hex[3] = {(unsigned char)(byte>>4), (unsigned char)(byte&0x0F), 0};
		hex[0] += (hex[0] < 10) ? '0' : ('A' - 10);
		hex[1] += (hex[1] < 10) ? '0' : ('A' - 10);
		
		test(bytes[i] == writeBytes[i], std::to_string(i) + ": " + (char *)hex);
	}
	test(bytes.size() == writeBytes.size(), "lengths match");
	test(true, "hell yeah");
}
