#include <uwebsockets/App.h>
#include <iostream>

int main() {
    /* The only job of this program is to test the listen function */
    uWS::App().listen("0.0.0.0", 9001, [](auto *listen_socket) {
        if (listen_socket) {
            std::cout << "Minimal test SUCCESS: Listening on port 9001." << std::endl;
        } else {
            std::cout << "Minimal test FAILED: Could not listen on port 9001." << std::endl;
        }
    }).run();

    std::cout << "This should not be printed if run() is successful." << std::endl;
}