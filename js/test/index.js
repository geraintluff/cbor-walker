let CBOR = require('../cbor.js');
let cborTests = require('./tests.js');

function assertDeepEqual(a, b) {
	if (Array.isArray(a)) {
		if (!Array.isArray(b)) throw Error("not equal");
		assertDeepEqual(a.length, b.length);
		a.forEach((ai, i) => assertDeepEqual(ai, b[i]));
	} else if (typeof a == 'object') {
		if (typeof b != 'object') throw Error("not equal");
		if (!a && !b) return;
		if (!a != !b) throw Error("not equal");
		assertDeepEqual(Object.keys(a).sort(), Object.keys(b).sort());
		for (let key in a) {
			assertDeepEqual(a[key], b[key]);
		}
	} else if (typeof a == 'number' && typeof b == 'number') {
		if (isNaN(a) && isNaN(b)) return;
		if (a !== b) throw Error("not equal");
	} else {
		if (a !== b) throw Error("not equal");
	}
}

cborTests.forEach(test => {
	if (!test) return; // removed (probably because Float16 isn't supported)
	let hex = test.cbor;
	let bytes = new Uint8Array(hex.length/2);
	for (let i = 0; i < bytes.length; ++i) bytes[i] = parseInt(hex.substr(i*2, 2), 16);
	let ab = bytes.buffer;
	
	console.log("test:", hex, test.data);
	let decoded = CBOR.decode16(hex);
	console.log(decoded);
	assertDeepEqual(test.data, decoded);
	
	if (test.roundTrip) {
		let hex2 = CBOR.encode16(test.data);
		if (hex2.toLowerCase() != hex.toLowerCase()) {
			console.error(hex, hex2);
			throw Error("round-trip encode doesn't match");
		}
	}
	
});

console.log('✅ passed');
