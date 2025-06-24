main: out/main
	@cd out && ./main
	
out/main: *.cpp ../*.h
	mkdir -p out
	g++ -std=c++17 -g -O3 \
		-Wall -Wextra -Wfatal-errors -Wpedantic -pedantic-errors \
		main.cpp -o out/main

clean:
	rm -rf out
