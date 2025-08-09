
# Trading System

High-performance, thread-safe C++ trading system with real-time WebSocket API, custom memory pool allocator, and order book matching engine.

## Features

- **Order Book:** Fast, time-priority matching for buy/sell orders
- **Custom Pool Allocator:** O(1) memory management for orders
- **Thread Safety:** Fine-grained locking with C++17 `std::shared_mutex`
- **WebSocket API:** Real-time trading, order management, and market data
- **Trade History:** Persistent log of all executed trades
- **Configurable:** Easy to extend for new order types or matching logic

## Getting Started

### Prerequisites

- C++17 compiler (g++, clang++)
- [uWebSockets](https://github.com/uNetworking/uWebSockets) (v20+)
- [nlohmann/json](https://github.com/nlohmann/json)
- zlib (for compression)

### Build Instructions


1. **Clone the repository:**
	 ```bash
	 git clone <your-repo-url>
	 cd trading_system
	 ```

2. **Install dependencies:**

### Dependency Installation

#### uWebSockets (v20+)
- **Option 1: Build from source**
	```bash
	git clone https://github.com/uNetworking/uWebSockets.git
	cd uWebSockets
	make
	```
	Then move the folder to `libs/uWebSockets` in your project.

- **Option 2: Homebrew (macOS)**
	```bash
	brew install uwebsockets
	```
	Add `/opt/homebrew/include` to your include path.

#### nlohmann/json
- **Option 1: Homebrew (macOS)**
	```bash
	brew install nlohmann-json
	```
- **Option 2: Manual download**
	Download [`json.hpp`](https://github.com/nlohmann/json/releases) and place it in `libs/nlohmann/json.hpp`.
	Add `-Ilibs/nlohmann` to your build command.

#### zlib
- **macOS:** Already included.
- **Linux:** Install with your package manager, e.g.:
	```bash
	sudo apt-get install zlib1g-dev
	```

**Note:**
Do **not** commit third-party libraries (`libs/uWebSockets`, `libs/nlohmann`) to your repository.
Add them to `.gitignore` and document installation steps here.


3. **Build the server:**
	```bash
	make
	```

To clean build artifacts:
```bash
make clean
```

## Usage

Start the trading server:
```bash
./trading_server
```

Connect via WebSocket (port 9001) and use JSON messages to:
- Authenticate
- Submit, modify, or cancel orders
- Query order status, order book, and trade history

## API Example

**Authentication:**
```json
{"type": "auth", "token": "your_secret_token"}
```

**Submit Order:**
```json
{"type": "submit", "price": 100.5, "qty": 10, "is_buy": true}
```

**Cancel Order:**
```json
{"type": "cancel", "id": 123}
```

**Get Order Book Snapshot:**
```json
{"type": "getOrderBookSnapshot"}
```

## Project Structure

- `order-book.cpp` — Order book and matching engine
- `pool_allocator.h` — Custom memory pool allocator
- `websocket.cpp` — WebSocket server and API
- `libs/uWebSockets/` — uWebSockets source and build
- `.vscode/` — VS Code configuration
- [`Makefile`](Makefile) — Build configuration

## Contributing

Pull requests are welcome! Please open issues for bugs or feature requests.

## License

MIT License. See [LICENSE](LICENSE) for details.

## Acknowledgements

- [uWebSockets](https://github.com/uNetworking/uWebSockets)
- [nlohmann/json](https://github.com/nlohmann/json)
