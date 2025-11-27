minify:
	npx uglify-js cbor.js -o cbor.min.js --compress passes=1 --mangle eval --mangle-props "regex=/^m_/"
	cat ./cbor.min.js > cbor.mjs; echo "export default CBOR;" >> cbor.mjs