#include "Server.hpp"

#ifndef FDCOUNT
    #define FDCOUNT 1024
#endif

bool isWorking;

void signal_handler(int num)
{
    if (num == SIGQUIT)
        isWorking = false;
}

bool check_num(std::string s) {
	size_t valid;

	valid = s.find_first_not_of("1234567890");
	if (valid != std::string::npos)
		return false;
	return true;
}

int main (int argc, char *argv[])
{
    //default parameters
    int port = 1000;
    std::string pass = "";

    Server *server;

    try {
        if (argc != 2 && argc != 3)
            throw std::runtime_error("invalid input! Input format: ./ircserv <port> <password>");
        else if (argc == 3)
        {
            if (check_num(argv[1]) == true)
                port = std::atoi(argv[1]);
            else
                throw std::runtime_error("invalid port, please enter only numbers");
            pass = argv[2];
        }
        else if (argc == 2) {
            if (check_num(argv[1]) == true)
                port = std::atoi(argv[1]);
            else
                throw std::runtime_error("invalid port, please enter only numbers");
        }
        server = new Server(port, pass);
    isWorking = true;
    if (signal(SIGQUIT, signal_handler) == SIG_ERR) {
        std::cout << "Error on signal init. Server stopped." << std::endl;
        isWorking = false;
    }
    else
        std::cout << "Server is working..." << std::endl;
    struct rlimit limits_old;
    if (!getrlimit(RLIMIT_NOFILE, &limits_old) && limits_old.rlim_cur < FDCOUNT) {
        struct rlimit limits_new;
        limits_new.rlim_cur = FDCOUNT;
        limits_new.rlim_max = FDCOUNT;
        setrlimit(RLIMIT_NOFILE, &limits_new);
    }
    while (isWorking) {
        try {
            server->refillSets();
            int ret = server->selector();
            if (ret < 0) {
                std::cout << ret << ": Select error, skip cycle\n";
                server->cleaner();
                continue ;
            }
            if (!ret)
                continue ;
            server->handleConnections();
            server->readRequests();
            server->sendResponses();
        }
        catch (const std::bad_alloc& ex) {
            server->cleaner();
        }
    }
    std::cout << "Server is shutting down..." << std::endl;
    delete server;
    setrlimit(RLIMIT_NOFILE, &limits_old);
    exit(0);
    } catch (const std::exception& e) {
        std::cout << e.what() << std::endl << " | Server stopped." << std::endl;
    }
}
