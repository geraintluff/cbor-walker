let CBORTypedArrays = (() => {
	// Decodes typed arrays from RFC-8746 tags
	function endianSwap(array, bytes) {
		for (let i = 0; i < array.length; i += bytes) {
			for (let k = 0; k < bytes/2; ++k) {
				let k2 = bytes - 1 - k;
				let tmp = array[i + k];
				array[i + k] = array[i + k2];
				array[i + k2] = tmp;
			}
		}
	}
	let bigEndian = new Uint8Array(new Uint16Array([256]).buffer)[0];
	let makeEndian = (TypedArray, be) => {
		const bpe = TypedArray.BYTES_PER_ELEMENT;
		return arr8 => {
			if (arr8.byteOffset%bpe) {
				arr8 = arr8.slice(); // fix alignment by making a copy
			}
			if (be != bigEndian) {
				endianSwap(arr8, bpe);
			}
			return new TypedArray(arr8.buffer, arr8.byteOffset, arr8.length/bpe);
		};
	};
	let typedArrayTags = {
		64: a => a,
		65: makeEndian(Uint16Array, true),
		66: makeEndian(Uint32Array, true),
		67: makeEndian(BigUint64Array, true),
		69: makeEndian(Uint16Array, false),
		70: makeEndian(Uint32Array, false),
		71: makeEndian(BigUint64Array, false),
		72: a => new Int8Array(a.buffer, a.byteOffset, a.length),
		73: makeEndian(Int16Array, true),
		74: makeEndian(Int32Array, true),
		75: makeEndian(BigInt64Array, true),
		77: makeEndian(Int16Array, false),
		78: makeEndian(Int32Array, false),
		79: makeEndian(BigInt64Array, false),
		81: makeEndian(Float32Array, true),
		82: makeEndian(Float64Array, true),
		85: makeEndian(Float32Array, false),
		86: makeEndian(Float64Array, false)
	};
	let taggedArrays = (v, tag) => {
		if (tag in typedArrayTags) {
			if (v instanceof Uint8Array) {
				return typedArrayTags[tag](v);
			}
		}
		return v;
	};
	function tagTypedArray(value) {
		if (!ArrayBuffer.isView(value)) return;
		if (value instanceof Uint8Array) {
			return {tag: 64, value: value};
		} else if (value instanceof Int8Array) {
			return {
				tag: 72,
				value: new Uint8Array(value.buffer, value.byteOffset, value.length)
			};
		} else if (value instanceof Uint16Array) {
			return {
				tag: (bigEndian ? 65 : 69),
				value: new Uint8Array(value.buffer, value.byteOffset, value.length*2)
			};
		} else if (value instanceof Int16Array) {
			return {
				tag: (bigEndian ? 73 : 77),
				value: new Uint8Array(value.buffer, value.byteOffset, value.length*2)
			};
		} else if (value instanceof Uint32Array) {
			return {
				tag: (bigEndian ? 66 : 70),
				value: new Uint8Array(value.buffer, value.byteOffset, value.length*4)
			};
		} else if (value instanceof Int32Array) {
			return {
				tag: (bigEndian ? 74 : 78),
				value: new Uint8Array(value.buffer, value.byteOffset, value.length*4)
			};
		} else if (value instanceof BigUint64Array) {
			return {
				tag: (bigEndian ? 67 : 71),
				value: new Uint8Array(value.buffer, value.byteOffset, value.length*8)
			};
		} else if (value instanceof BigInt64Array) {
			return {
				tag: (bigEndian ? 75 : 79),
				value: new Uint8Array(value.buffer, value.byteOffset, value.length*8)
			};
		} else if (value instanceof Float32Array) {
			return {
				tag: (bigEndian ? 81 : 85),
				value: new Uint8Array(value.buffer, value.byteOffset, value.length*4)
			};
		} else if (value instanceof Float64Array) {
			return {
				tag: (bigEndian ? 82 : 86),
				value: new Uint8Array(value.buffer, value.byteOffset, value.length*8)
			};
		}
	}
	return {
		tagToArray: taggedArrays,
		arrayToTag: tagTypedArray
	};
})();
