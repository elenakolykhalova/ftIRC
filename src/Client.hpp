#ifndef CLIENT_HPP
#define CLIENT_HPP
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#ifndef __APPLE__
    #include <wait.h>
#endif

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include "TextHolder.hpp"
class Server;
class Channel;
enum Status{
    waitForPass,
    waitForNick,
    waitForRequest,
    waitForResponse,
    waitForResponseChain,
    Error,
    Quit,
    Null
};

class Client{

private:
    
    int descriptor;
    struct timeval timer;
    struct sockaddr_in addr;
    char ip_address_str[INET_ADDRSTRLEN];
    
    int responseSize;
    int responsePos;
    
    size_t requestLen;
    Status status;

    TextHolder *buffer;
    std::map<std::string, Client*> *allusers;
    int code;
    
    bool isAuthorized;
	bool Away;
    
    std::string nickname;
    std::string tempNickname;
    std::string username;
    std::string realname;
    std::string identifier;
    std::string victimName;
	std::string conctd;
	std::string awaystatus;
	std::string target_nickname;
    
    Server *server;
    
    size_t chainCounter;
    std::map<std::string, Channel *>::iterator channel;
    Status reservedStatus;
    bool targetToChannel;
    
    std::map<std::string, Channel *> mychannels;
    Client *privateChat;
    bool needNoChain;

	Client(void);
	Client(Client const &other);
	Client &operator=(Client const &other);

public:
    Client(int &port, std::map<std::string, Client *> &users, Server *_server);
    ~Client();
    void setTimer();
    TextHolder *getBuffer();
    void resetBuffer();
    int &getDescriptor();
    Status getStatus();
    struct timeval &getTimer();
    void handleRequest(std::string const &str);
    void formResponse(std::string const &str);
    void sendResponse();
    bool isInMap();
    std::string &getNick();
    bool fillUserData();
    void bufferNick();
    bool joinChannel();
    bool leaveChannel();
    bool exitChannel(std::string const &name);
    void leaveAllChannels();
    void setStatus(Status st);
    void outerRefillBuffer(std::string const & str);
    void handleReserved();
    bool handleMessage();
	bool handleMessage2();
	bool WhoList();

    bool WhoIs();

	bool setAway();
	bool getAway();
	std::string  const & getAwayStatus();
    std::string &getID();
    bool readyForReserve();
    void addChannel(std::string const &name, Channel *c);
    bool nickIsAcceptable();
    bool updateTopic();
    bool handleMode();
    std::string const getChannelUsersList() const;
    bool executeKick();
    bool executeBan(bool add);
    std::string const checkForPassword() const;
    bool handleNickChange();
    void broadcastToAllChannels();
    bool printBanList(std::string channelName, bool add);
	bool handleInvite();
};

#include "Server.hpp"
#endif
