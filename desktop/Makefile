SRC = ../core/sy909.cpp $(wildcard ../core/unsp/unsp*.cpp)
probe: probe.cpp $(SRC) ../core/*.h ../core/unsp/*.h
	g++ -std=c++20 -O3 -I../core -I../core/unsp -o probe probe.cpp $(SRC)
