CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Ilibs/uWebSockets/src -Ilibs/uWebSockets/uSockets/src -I/opt/homebrew/include
LDFLAGS = libs/uWebSockets/uSockets/*.o -lz
SRC = websocket.cpp order-book.cpp
TARGET = trading_server

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) $(LDFLAGS) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean