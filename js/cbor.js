let CBOR = {
	encode(value, optionalBuffer) {
		let currentBuffer = (optionalBuffer && optionalBuffer.resizable)
			? (optionalBuffer.resize(0), optionalBuffer)
			: new ArrayBuffer(0, {maxByteLength: 64});
		let data = new DataView(currentBuffer);
		let buffers = [currentBuffer];
		
		let encodeTag = CBOR.encodeTag;
		
		function writeHead(typeCode, remainder, extraBytes) {
			let bytes = 1 + extraBytes;
			if (currentBuffer.byteLength + bytes > currentBuffer.maxByteLength) {
				let capacity = 1;
				while (capacity <= currentBuffer.maxByteLength || capacity <= extraBytes) capacity *= 2;
				currentBuffer = new ArrayBuffer(0, {maxByteLength: capacity});
				buffers.push(currentBuffer);
				data = new DataView(currentBuffer);
			}
			let index = currentBuffer.byteLength;
			currentBuffer.resize(index + bytes);
			data.setUint8(index, (typeCode<<5)|remainder);
			return index + 1;
		}
		function writeUint(typeCode, value, extraBytes) {
			if (value < 24) {
				return writeHead(typeCode, value, extraBytes);
			} else if (value < 256) {
				let index = writeHead(typeCode, 24, extraBytes + 1);
				data.setUint8(index, value);
				return index + 1;
			} else if (value < 65536) {
				let index = writeHead(typeCode, 25, extraBytes + 2);
				data.setUint16(index, value);
				return index + 2;
			} else {
				let index = writeHead(typeCode, 26, extraBytes + 4);
				data.setUint32(index, value);
				return index + 4;
			}
		}
		function write(value) {
			if (typeof value === 'number') {
				if (Number.isInteger(value) && value <= Number.MAX_SAFE_INTEGER && value >= Number.MIN_SAFE_INTEGER) {
					if (value >= 0) {
						if (value < 4294967296) {
							writeUint(0, value, 0);
						} else if (value < 18446744073709551616) {
							let index = writeHead(0, 27, 8);
							data.setBigUint64(index, BigInt(value));
						} else {
							// write as float
							let index = writeHead(7, 27, 8);
							data.setFloat64(index, value);
						}
					} else {
						if (value >= -4294967296) {
							writeUint(1, -1 - value, 0);
						} else if (value >= -18446744073709551616n) {
							let index = writeHead(1, 27, 8);
							data.setBigUint64(index, BigInt(-1 - value));
						} else {
							// write as float
							let index = writeHead(7, 26, 8);
							data.setFloat64(index, value);
						}
					}
				} else {
					let index = writeHead(7, 27, 8);
					data.setFloat64(index, value);
				}
			} else if (typeof value === 'bigint') {
				if (value >= 0) {
					if (value < 18446744073709551616n) {
						let index = writeHead(0, 27, 8);
						data.setBigUint64(index, value);
					} else {
						writeHead(6, 2, 0); // semantic tag 2
						let bytes = [];
						while (value) {
							bytes.unshift(Number(value&0xFFn));
							value >>= 8n;
						}
						write(new Uint8Array(bytes).buffer);
					}
				} else {
					if (value >= -18446744073709551616n) {
						let index = writeHead(1, 27, 8);
						data.setBigUint64(index, -1n - value);
					} else {
						writeHead(6, 3, 0); // semantic tag 3
						value = -1n - value;
						let bytes = [];
						while (value) {
							bytes.unshift(Number(value&0xFFn));
							value >>= 8n;
						}
						write(new Uint8Array(bytes).buffer);
					}
				}
			} else if (value === false) {
				writeHead(7, 20, 0);
			} else if (value === true) {
				writeHead(7, 21, 0);
			} else if (value === null) {
				writeHead(7, 22, 0);
			} else if (value === void 0) {
				writeHead(7, 23, 0);
			} else if (typeof value === "string") {
				let array8 = new TextEncoder().encode(value);
				let index = writeUint(3, array8.length, array8.length);
				(new Uint8Array(currentBuffer)).set(array8, index);
			} else if (value instanceof ArrayBuffer) {
				let index = writeUint(2, value.byteLength, value.byteLength);
				// Copy bytes over
				(new Uint8Array(currentBuffer)).set(new Uint8Array(value), index);
			} else if (Array.isArray(value)) {
				writeUint(4, value.length, 0);
				value.forEach(write);
			} else if (typeof value === 'object') {
				if (encodeTag in value) {
					let pair = value[encodeTag](value);
					writeUint(6, pair[0], 0); // semantic tag
					return write(pair[1]);
				} else { // write it as a map
					let keys = Object.keys(value);
					writeHead(5, keys.length, 0);
					keys.forEach(key => {
						write(key);
						write(value[key]);
					});
				}
			} else {
				for (let i = 0; i < 256; ++i) {
					if (value === CBOR.simple[i]) {
						return writeUint(7, i, 0);
					}
				}
				throw Error("unknown simple value");
			}
		}
		write(value);
		
		if (buffers.length == 1) return buffers[0];
		
		let totalLength = 0;
		buffers.forEach(b => totalLength += b.length);
		let result = new ArrayBuffer(0, {maxByteLength: totalLength});
		let array8 = new Uint8Array(result);
		buffers.forEach(b => {
			let index = result.byteLength;
			result.resize(index + b.byteLength);
			array8.set(new Uint8Array(b), index);
		});
		return result;
	},

	decode(data) {
		let tags = CBOR.decodeTags;
		let index = 0;
		if (ArrayBuffer.isView(data)) {
			index = data.byteOffset;
			data = data.buffer;
		}
		let maxIndex = data.byteLength;
		data = new DataView(data);
		
		let breakCode = CBOR.breakCode;
		
		function makeValue(typeCode, additional, uint8Bytes) {
			switch (typeCode) {
				case 0: return additional;
				case 1: return -1 - additional;
				case 2: {
					let result = uint8Bytes ? new Uint8Array(data.buffer, index, additional) : data.buffer.slice(index, index + additional);
					index += additional;
					return result;
				}
				case 3: {
					let slice = new Uint8Array(data.buffer, index, additional);
					index += additional;
					return new TextDecoder().decode(slice);
				}
				case 4: {
					let array = [];
					for (let i = 0; i < additional; ++i) {
						array.push(next());
					}
					return array;
				}
				case 5: {
					let map = Object.create(null);
					for (let i = 0; i < additional; ++i) {
						map[next()] = next();
					}
					return map;
				}
				case 6: // semantic tag
					if (tags[additional]) {
						return tags[additional](next(true));
					}
					return next();
				case 7: // simple value
					if (additional >= 256) throw Error("invalid simple value");
					return CBOR.simple[additional];
				default:
			}
		}
		function makeValue64(typeCode, additional, uint8Bytes) {
			switch (typeCode) {
				case 0: return additional;
				case 1: return -1n - additional;
				default:
					throw Error("unsupported type for 8-byte length");
			}
		}
		function makeIndefinite(typeCode, uint8Bytes) {
			switch (typeCode) {
				case 2: {
					let buffers = [];
					let length = 0;
					let item = next(true); // returns the bytes as Uint8Array
					while (item != breakCode) {
						if (!(item instanceof Uint8Array)) throw Error("indefinite bytes with non-byte item");
						buffers.push(item);
						length += item.length;
						item = next(true);
					}
					let array8 = new Uint8Array(length);
					let index = 0;
					buffers.forEach(b => {
						array8.set(b, index);
						index += b.length;
					});
					return uint8Bytes ? array8 : array8.buffer;
				}
				case 3: {
					let result = "";
					let item = next();
					while (item != breakCode) {
						if (typeof item !== 'string') throw Error("indefinite string with non-string item");
						result += item;
						item = next();
					}
					return result;
				}
				case 4: {
					let array = [];
					let item = next();
					while (item != breakCode) {
						array.push(item);
						item = next();
					}
					return array;
				}
				case 5: {
					let map = Object.create(null);
					let item = next();
					while (item != breakCode) {
						map[item] = next();
						item = next();
					}
					return map;
				}
				case 7: return breakCode;
				default: throw Error("invalid indefinite type");
			}
		}
		function next(uint8Bytes=false) {
			if (index >= maxIndex) throw Error("end of CBOR");
			let head = data.getUint8(index++);
			let typeCode = head>>5, remainder = head&0x1f;
			switch(remainder) {
				case 24: return makeValue(typeCode, data.getUint8(index++), uint8Bytes);
				case 25:
					if (typeCode == 7) {
						let v = data.getFloat16(index);
						index += 2;
						return v;
					} else {
						let additional = data.getUint16(index);
						index += 2;
						return makeValue(typeCode, additional, uint8Bytes);
					}
				case 26: {
					if (typeCode == 7) {
						let v = data.getFloat32(index);
						index += 4;
						return v;
					} else {
						let additional = data.getUint32(index);
						index += 4;
						return makeValue(typeCode, additional, uint8Bytes);
					}
				}
				case 27: {
					if (typeCode == 7) {
						let v = data.getFloat64(index);
						index += 4;
						return v;
					} else {
						let additional = data.getBigUint64(index);
						index += 4;
						if (additional < Number.MAX_SAFE_INTEGER) {
							return makeValue(typeCode, Number(additional), uint8Bytes);
						} else {
							return makeValue64(typeCode, additional, uint8Bytes);
						}
					}
				}
				case 28:
				case 29:
				case 30:
					throw Error("invalid additional CBOR code");
				case 31:
					return makeIndefinite(typeCode, uint8Bytes);
				default:
					return makeValue(typeCode, remainder, uint8Bytes);
			}
		}
		return next();
	},
	
	// Simple values
	simple: [],

	// Semantic tags
	encodeTag: Symbol(), // if defined on an object, function mapping: value => [tag, value]
	decodeTags: {
		0: isoString => new Date(isoString),
		1: epochSeconds => new Date(epochSeconds*1000),
		2: bytes => {
			let result = 0n;
			for (let i = 0; i < bytes.length; ++i) {
				result = result*256n + BigInt(bytes[i]);
			}
			return result;
		},
		3: bytes => {
			let result = 0n;
			for (let i = 0; i < bytes.length; ++i) {
				result = result*256n + BigInt(bytes[i]);
			}
			return -1n - result;
		},
		32: url => new URL(url),
		64: uint8 => uint8,
		72: uint8 => new Int8Array(uint8.buffer, uint8.byteOffset, uint8.length),
		258: array => new Set(array)
	},
	
	// Helpers for hex / base64 encoding
	encode16(value, optionalBuffer) {
		let bytes = new Uint8Array(CBOR.encode(value, optionalBuffer));
		let hexString = '';
		for (let i = 0; i < bytes.length; ++i) {
			hexString += (bytes[i]>>4).toString(16) + (bytes[i]&15).toString(16);
		}
		return hexString;
	},
	encode64(value, optionalBuffer) {
		let bytes = new Uint8Array(CBOR.encode(value, optionalBuffer));
		let binaryString = '';
		for (let i = 0; i < bytes.length; ++i) binaryString += String.fromCharCode(bytes[i]);
		return btoa(binaryString);
	},
	decode64(base64) {
		let binaryString = atob(base64);
		let bytes = new Uint8Array(binaryString.length);
		for (let i = 0; i < bytes.length; ++i) bytes[i] = binaryString.charCodeAt(i);
		return CBOR.decode(bytes);
	},
	decode16(hex) {
		let bytes = new Uint8Array(hex.length/2);
		for (let i = 0; i < bytes.length; ++i) bytes[i] = parseInt(hex.substr(i*2, 2), 16);
		return CBOR.decode(bytes);
	}
};
Date.prototype[CBOR.encodeTag] = date => [1, +date/1000];
URL.prototype[CBOR.encodeTag] = url => [32, url.href];
Set.prototype[CBOR.encodeTag] = s => [258, Array.from(s)];
Uint8Array.prototype[CBOR.encodeTag] = array => [64, array.buffer];
Int8Array.prototype[CBOR.encodeTag] = array => [72, array.buffer];
for (let i = 0; i < 256; ++i) CBOR.simple[i] = Symbol(`CBOR ${i}`);
CBOR.simple[20] = false;
CBOR.simple[21] = true;
CBOR.simple[22] = null;
CBOR.simple[23] = void 0;
