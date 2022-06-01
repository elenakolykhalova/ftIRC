#include "Client.hpp"
#include "Channel.hpp"
#include "Server.hpp"

Client::Client(int &port, std::map<std::string, Client *> &users, Server *_server): allusers(&users), nickname(""), username(""), realname(""), server(_server) {
    int addrLen = sizeof(addr);
    descriptor = accept(port, (sockaddr *) &addr, (socklen_t *)&addrLen);
    if (descriptor < 0)
        throw Exception("Client connection exception");
    struct linger so_linger;
    so_linger.l_onoff = true;
    so_linger.l_linger = 0;
    setsockopt(descriptor, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));
    if (fcntl(descriptor, F_SETFL, O_NONBLOCK) < 0)
        throw Exception("Client fcntl exception");
    requestLen = 0;
    if (server->hasPassword()) {
        status = waitForPass;
        std::cout << "wait for pass\n";
    } else {
        status = waitForNick;
        std::cout << "wait for nick\n";
    }
    reservedStatus = Null;
    responsePos = 0;
    gettimeofday(&timer, 0);
    buffer = new TextHolder();
    code = 0;
    isAuthorized = false;
    struct in_addr ipAddr = addr.sin_addr;
    inet_ntop(AF_INET, &ipAddr, ip_address_str, INET_ADDRSTRLEN );
    chainCounter = 0;
    targetToChannel = false;
    privateChat = NULL;
    needNoChain = false;
	conctd.clear();
	Away = false;
	awaystatus = "";
    std::cout << "Client_" << descriptor << " connected\n";
}
    
Client::~Client() {
    std::cout << "Client_" << descriptor << " has disconnected\n";
    leaveAllChannels();
    close(descriptor);
    delete buffer;
    buffer = NULL;
    privateChat = NULL;
}

void Client::setTimer() {
    gettimeofday(&timer, 0);
}

TextHolder *Client::getBuffer() {
    return (buffer);
}

void Client::resetBuffer() {
    delete buffer;
    buffer = new TextHolder();
}

int &Client::getDescriptor() {
    return (descriptor);
}

Status Client::getStatus() {
    return (status);
}

struct timeval &Client::getTimer() {
    return (timer);
}

void Client::handleRequest(std::string const &str) {
    std::cout << "\n\033[1;31mClient_" << descriptor << " | " << nickname << " | RECEIVED: <\033[0m" << buffer->getBuffer() << "\033[1;31m>END\033[0m\n";
    if (buffer->isQuit()) {
        status = Quit;
        leaveAllChannels();
        return ;
    }
    if (username.empty() && fillUserData())
    {
        if (username.empty())
            username = nickname;
        if (realname.empty())
            realname = nickname;
        if (code)
        {
            formResponse(str);
            return ;
        }
    }
    
    if (status == waitForPass) {
        std::cout << "Checking password\n";
        std::string const &passResult = checkForPassword();
        if (server->passwordMatch(passResult)) {
            code = 2;
            std::cout << "Pass ok\n";
        } else {
            code = 464;
            std::cout << "Pass no ok\n";
        }
    } else if (status == waitForNick) {
        tempNickname.clear();
        bufferNick();
        if (nickIsAcceptable()) {
            if (allusers->count(tempNickname))
                code = 433;
            else {
                nickname = tempNickname;
                tempNickname.clear();
                allusers->insert(std::make_pair(nickname, this));
                isAuthorized = true;
                code = 1;
            }
        }
    } else if (status == waitForRequest) {
        if (buffer->isList()) {
            chainCounter = server->getChannelsList()->size();
            channel = server->getChannelsList()->begin();
            if (channel != server->getChannelsList()->end())
                code = 322;
            else
                code = 323;
        } else if (buffer->isJoin()) {
            if (!joinChannel()) {
                buffer->clear();
                return ;
            }
        } else if (buffer->isMsg()) {
            if (!handleMessage()) {
                buffer->clear();
                return ;
            }
        }
        else if (buffer->isPart()) {
            if (!leaveChannel()) {
                buffer->clear();
                return ;
            }
        } else if (buffer->isTopic()) {
            if (!updateTopic()) {
                buffer->clear();
                return ;
            }
        } else if (buffer->isMode()) {
            if (!handleMode()) {
                buffer->clear();
                return ;
            }
        } else if (buffer->isKick()) {
            if (!executeKick()) {
                buffer->clear();
                return ;
            }
        } else if (buffer->isNick()) {
            if (!handleNickChange()) {
                buffer->clear();
                return ;
            }
		} else if (buffer->isAway()) {
			if (!setAway()) {
				buffer->clear();
				return ;
			}
		} else if (buffer->isNotice()) {
			if (!handleMessage2()) {
				buffer->clear();
				return ;
			}
		} else if (buffer->isWho()) {
			if (!WhoList()) {
				buffer->clear();
				return ;
			}
		} else if (buffer->isInvite()) {
			if (!handleInvite()) {
				buffer->clear();
				return ;
			}
        } else {
            buffer->clear();
            return ;
        }
    }
    formResponse(str);
}

void Client::formResponse(std::string const &str) {
	if (code == 301)
		std::cout << buffer->getBufferReserved() << std::endl;
    else if (code == 330 || targetToChannel || code == 333 || code == 335 || code == 336) {
        buffer->fillMessage(":" + identifier + " " + buffer->getBuffer().substr(0, buffer->bufferSize() - 2) + "\n\r\n\n");
        if (code == 335) {
            buffer->fillMessage(":" + identifier + " MODE " + (*channel).first + " +o " + nickname + "\n\r\n\n");
            code = 330;
        }
        std::map<std::string, Client *>::iterator it = (*channel).second->getUsers()->begin();
        while (it != (*channel).second->getUsers()->end()) {
           
            if ((*it).second != this || code == 330 || code == 334 || code == 336) {
                (*it).second->outerRefillBuffer(buffer->getMessage());
                if (code == 336 && victimName == (*it).first) {
                    (*it).second->exitChannel((*channel).first);
                    std::map<std::string, Client *>::iterator prevIt = it++;
                    (*channel).second->removeUser((*prevIt).first);
                    continue ;
                }
            }
            it++;
        }
    
        if (code == 333)
            outerRefillBuffer(buffer->getMessage());
        targetToChannel = false;
    } else if (code == 337) {
        buffer->fillMessage(":" + identifier + " " + buffer->getBuffer().substr(0, buffer->bufferSize() - 2) + "\n\r\n\n");
        identifier = nickname + "!" + username + "@" + ip_address_str;
        broadcastToAllChannels();
    } else if (privateChat != NULL) {
        privateChat->outerRefillBuffer(":" + identifier + " " + buffer->getBuffer().substr(0, buffer->bufferSize() - 2) + "\n\r\n\n");
        privateChat = NULL;
        buffer->clear();
    } else if (reservedStatus == Null){
        resetBuffer();
        buffer->fillBuffer(str);
    }
    switch (code) {
        case 1: {
            if (!username.empty()) {
                identifier = nickname + "!" + username + "@" + ip_address_str;
                buffer->fillBuffer(" 001 " + nickname + " :Welcome to the Internet Relay Network " + identifier + "\n\r\n\n");
            } else {
                resetBuffer();
                status = waitForRequest;
                return ;
            }
            break ;
        }
        case 322: {
            buffer->fillBuffer(" 322 " + nickname + " " + (*channel).first + " " + (*channel).second->getUsersNumberStr() + " :" + (*channel).second->getTopic() + "\n\r\n\n");
            break ;
        }
        case 323: {
            buffer->fillBuffer(" 323 " + nickname + "\n\r\n\n");
            break ;
        }
        case 331: {
            buffer->fillBuffer(" 331 " + nickname + " " + (*channel).first + " :No topic is set\n\r\n\n");
            break ;
        }
        case 332: {
            buffer->fillBuffer(" 332 " + nickname + " " + (*channel).first + " :" + (*channel).second->getTopic() + "\n\r\n\n");
            break ;
        }
		case 341: {
			buffer->fillBuffer(":localhost 341 " + nickname + " " + target_nickname + " " + (*channel).first + "\n\r\n\n");
			break ;
		}
        case 353: {
            buffer->fillBuffer(" 353 " + nickname + " = " + (*channel).first + " :");
            buffer->fillBuffer(getChannelUsersList());
            break ;
        }
        case 366: {
            buffer->fillBuffer(" 366 " + nickname + " " + (*channel).first + " :End of NAMES list\n\r\n\n");
            break ;
        }
		case 367: {
			buffer->fillBuffer(" 367 " + nickname + " :" + (*channel).first + " " + conctd + "\n\r\n\n");
			conctd.clear();
			break;
		}
		case 368: {
			buffer->fillBuffer(" 368 " + nickname + " :" + (*channel).first + " :End of channel ban list" + "\n\r\n\n");
			break ;
		}
        case 431: {
            buffer->fillBuffer(" 431 * :No nickname given\n\r\n\n");
            break ;
        }
        case 432: {
            buffer->fillBuffer(" 432 * " + nickname + " :Erroneous nickname\n\r\n\n");
            break ;
        }
        case 433: {
            buffer->fillBuffer(" 433 * " + nickname + " :Nickname is already in use\n\r\n\n");
            break ;
        }
		case 442: {
			buffer->fillBuffer(" 442 * " + (*channel).first + " :You're not on that channel\n\r\n\n");
			break ;
		}
		case 443: {
			buffer->fillBuffer(" 443 * " + target_nickname + " " + (*channel).first + " :is already on channel\n\r\n\n");
			break ;
		}
        case 464: {
            buffer->fillBuffer(" 464 * :Password incorrect\n\r\n\n");
            break ;
        }
        case 474: {
            buffer->fillBuffer(" 474 * " + (*channel).first + " :Cannot join channel (+b)\n\r\n\n");
            break ;
        }
        case 475: {
            buffer->fillBuffer(" 475 * " + (*channel).first + " :Cannot join channel (+k)\n\r\n\n");
            break ;
        }
		case 500: {
			std::string tmp = "";
			for (std::map<std::string, Client*>::iterator itC = allusers->begin(); itC != allusers->end(); itC++)
			{
				tmp += "\n" + itC->first;
			}
			tmp+="\n\n";
			buffer->fillBuffer(tmp);
			break ;
		}
		case 306: {
			buffer->fillBuffer(" :You have been marked as being away \n\r");
			break ;
		}
		case 305: {
			buffer->fillBuffer(" :You are no longer marked as being away \n\r");
			awaystatus.clear();
			break ;
		}
		case 301: {
			break ;
		}


    }
    responsePos = 0;
    responseSize = buffer->bufferSize();
    status = waitForResponse;
}

void Client::sendResponse()
{
    if (buffer->empty())
    {
        status = waitForRequest;
        if (reservedStatus == Null)
            resetBuffer();
        else
            handleReserved();
        return ;
    }
	std::cout << "\n\033[1;32mCODE: " << code << " | RESPONSE TO " << nickname << " IS: <\033[0m" << buffer->getBuffer() << "\033[1;32m>END\033[0m\n";
    int send_size = send(descriptor, buffer->getBuffer().substr(responsePos, responseSize - responsePos - 1).c_str(), responseSize - responsePos - 1, 0);
    if (send_size <= 0) {
        status = Error;
        return ;
    }
    responsePos += send_size;
    if (responsePos == responseSize - 1) {
        if (code > 430 && code < 437 && !isAuthorized) {
            status = waitForNick;
        } else if (code == 322) {
            status = waitForResponseChain;
            channel++;
            if (channel == server->getChannelsList()->end())
                code++;
        } else if (code == 330) {
            status = waitForResponseChain;
            if ((*channel).second->getTopic().empty())
                code = 331;
            else
                code = 332;
        } else if ((code == 331 || code == 332) && !needNoChain) {
            status = waitForResponseChain;
            code = 353;
        } else if (code == 353) {
            status = waitForResponseChain;
            code = 366;
        } else if (code == 367) {
			status = waitForResponseChain;
			code = 368;
		} else if (code == 2) {
            status = waitForNick;
        } else if (code == 464) {
            status = waitForPass;
        } else {
            status = waitForRequest;
            needNoChain = false;
        }
        if (reservedStatus == Null)
            resetBuffer();
        else
			handleReserved();
    }
}

bool Client::isInMap()
{
    return (isAuthorized);
}

std::string &Client::getNick()
{
    return (nickname);
}

bool Client::fillUserData() {
    size_t pos1 = 0, pos2 = 0;
    pos1 = buffer->getBuffer().find("USER ");
    if (pos1 == std::string::npos)
        return (false);
    pos1 += 5;
    while ((buffer->getBuffer())[pos1] == ' ')
        pos1++;
    pos2 = buffer->getBuffer().find(" ", pos1 + 1);
    if (pos2 == std::string::npos)
        return (false);
    username.clear();
    username = buffer->getBuffer().substr(pos1, pos2 - pos1);
    while ((pos1 = buffer->getBuffer().find(" ", pos2)) != std::string::npos)
        pos2 = pos1 + 1;
    pos2--;
    pos1 = buffer->getBuffer().find("\r\n", pos2);
    if (pos1 == std::string::npos)
    {
        username.clear();
        return (false);
    }
    realname.clear();
    realname = buffer->getBuffer().substr(pos2, pos1 - pos2);
    return (true);
}

void Client::bufferNick() {
    size_t pos1 = 0, pos2 = 0;
    pos1 = buffer->getBuffer().find("NICK ");
    if (pos1 == std::string::npos) {
        tempNickname = "";
        return ;
    }
    pos1 += 5;
    pos2 = buffer->getBuffer().find("\r\n", pos1);
    if (pos2 == std::string::npos) {
        tempNickname = "";
        return ;
    }
    tempNickname = buffer->getBuffer().substr(pos1, pos2 - pos1);
}

bool Client::joinChannel() {
    size_t pos1 = 5;
    size_t pos2 = buffer->getBuffer().find("\r\n", pos1);
    if (pos2 == std::string::npos)
        return (false);
    std::string channelName, topic, password;
    size_t pos3 = buffer->getBuffer().find(" :", pos1);
    if (pos3 != std::string::npos) {
        topic = buffer->getBuffer().substr(pos3 + 2, pos2 - pos3 - 2);
        pos2 = pos3;
    } else
        topic = "";
    size_t pos4 = buffer->getBuffer().find(" ", pos1);
    if (pos4 != std::string::npos && pos4 != pos3) {
        password = buffer->getBuffer().substr(pos4 + 1, pos2 - pos4 - 1);
        pos2 = pos4;
    } else
        password = "";
    
        
    channelName = buffer->getBuffer().substr(pos1, pos2 - pos1);
    if (channelName[0] != '#' || mychannels.find(channelName) != mychannels.end())
        return (false);
    channel = server->getChannelsList()->find(channelName);
    if (channel != server->getChannelsList()->end())
    {
        if ((*channel).second->isBanned(nickname, ip_address_str)) {
            code = 474;
            return (true);
        }
        if (!(*channel).second->isPasswordMatched(password)) {
            code = 475;
            return (true);
        }
        (*channel).second->addUser(this);
        mychannels[(*channel).first] = (*channel).second;
        if ((*channel).second->isOperator(nickname))
        {
            code = 335;
            return (true);
        }
    } else {
        server->createChannel(channelName, this);
        channel = server->getChannelsList()->find(channelName);
        (*channel).second->setPassword(password);
        (*channel).second->setTopic(topic);
    }
    code = 330;
    return (true);
}

bool Client::leaveChannel() {
    size_t pos1 = 5;
    size_t pos2 = buffer->getBuffer().find(" :", pos1);
    if (pos2 == std::string::npos && (pos2 = buffer->getBuffer().find("\r\n", pos1)) == std::string::npos)
        return (false);
    std::string channelName;
    size_t pos3 = buffer->getBuffer().find(":", pos1);
    if (pos3 != std::string::npos && pos3 < pos2)
        channelName = buffer->getBuffer().substr(pos1, pos3 - pos1);
    else
        channelName = buffer->getBuffer().substr(pos1, pos2 - pos1);
    channel = server->getChannelsList()->find(channelName);
    if (channel != server->getChannelsList()->end()) {
        (*channel).second->removeUser(nickname);
        return (exitChannel((*channel).first));
    }
    return (false);
}

bool Client::exitChannel(std::string const &name) {
    if (mychannels.erase(name))
    {
        code = 333;
        return (true);
    }
    return (false);
}

void Client::leaveAllChannels() {
    std::map<std::string, Channel *>::iterator it1 = mychannels.begin();
    while (it1 != mychannels.end())
    {
        (*it1).second->removeUser(nickname);
        std::string quitMessage = ":" + identifier + " PART " + (*it1).first + " :user disconnected from server\n\r\n\n";
        std::map<std::string, Client *>::iterator it2 = (*channel).second->getUsers()->begin();
        while (it2 != (*channel).second->getUsers()->end()) {
            (*it2).second->outerRefillBuffer(quitMessage);
            it2++;
        }
        it1++;
    }
    mychannels.clear();
}

void Client::setStatus(Status st) {
    status = st;
}

void Client::outerRefillBuffer(std::string const &str) {
    if (!buffer->empty()) {
        reservedStatus = waitForResponse;
        buffer->fillBufferReserved(str);
    } else {
        status = waitForResponse;
        buffer->fillBuffer(str);
        responsePos = 0;
        responseSize = buffer->bufferSize();
    }
}

void Client::handleReserved() {
    if (reservedStatus == Null)
        return ;
    std::swap(status, reservedStatus);
    if (status == Quit)
        return ;
    buffer->handleReserved();
    if (buffer->reserveIsEmpty())
        reservedStatus = Null;
    responsePos = 0;
    responseSize = buffer->bufferSize();
}

bool Client::handleMessage2() {
	size_t pos1 = 7;
    size_t pos2 = buffer->getBuffer().find(" ", pos1);
    if (pos2 == std::string::npos)
        return (false);
	std::string target;
    size_t pos3 = buffer->getBuffer().find(":", pos1);
    if (pos3 != std::string::npos && pos3 < pos2)
        target = buffer->getBuffer().substr(pos1, pos3 - pos1);
    else
        target = buffer->getBuffer().substr(pos1, pos2 - pos1);
    if (target[0] == '#') {
        channel = mychannels.find(target);
        if (channel != mychannels.end())
            targetToChannel = true;
        else
            return (false);
    } else {
        privateChat = server->findUser(target);
        if (privateChat == NULL) {
            privateChat = this;
            buffer->clear();
            buffer->fillBuffer("PRIVMSG " + target + " :No such user - " + target + "\r\n");
            pos2 = buffer->getBuffer().find(" :");
        }
    }
    pos2++;
    pos1 = buffer->getBuffer().find("\r\n", pos2);
    if (pos1 == std::string::npos) {
        targetToChannel = false;
        return (false);
    }
    pos1+=2;
    code = 0;
    if (buffer->getBuffer()[pos2] != ':')
        buffer->insert(pos2, ':');
    return (true);
}

bool Client::handleMessage() {
	size_t pos1 = 8;
	size_t pos2 = buffer->getBuffer().find(" ", pos1);
	if (pos2 == std::string::npos)
		return (false);
	std::string target;
	size_t pos3 = buffer->getBuffer().find(":", pos1);
    if (buffer->getBuffer()[pos1] != '#')
    {
        if (pos3 != std::string::npos && pos3 < pos2)
            target = buffer->getBuffer().substr(pos1, pos3 - pos1);
        else
        {
            target = buffer->getBuffer().substr(pos1, pos2 - pos1);
            Client *trgt = server->findUser(target);
            if (!trgt->getAwayStatus().empty())
            {
                buffer->clear();
                buffer->fillBuffer(target + " :" + trgt->getAwayStatus() + "\r\n");
                code = 301;
                return true;
            }
        }
    }
	if (buffer->getBuffer()[pos1] == '#') {
        target = buffer->getBuffer().substr(pos1, pos2 - pos1);
		channel = mychannels.find(target);
		if (channel != mychannels.end())
			targetToChannel = true;
		else
			return (false);
	} else {
		privateChat = server->findUser(target);
		if (privateChat == NULL) {
			privateChat = this;
			buffer->clear();
			buffer->fillBuffer("PRIVMSG " + target + " :No such user - " + target + "\r\n");
			pos2 = buffer->getBuffer().find(" :");
		}
	}
	pos2++;
	pos1 = buffer->getBuffer().find("\r\n", pos2);
	if (pos1 == std::string::npos) {
		targetToChannel = false;
		return (false);
	}
	pos1+=2;
	code = 0;
	if (buffer->getBuffer()[pos2] != ':')
		buffer->insert(pos2, ':');
	return (true);
}

bool Client::WhoList(){

	code = 500;
	return (true);
}

bool Client::setAway(){
	if (buffer->bufferSize() <= 7)
	{
		code = 305;
		Away = false;
	}
	else if (buffer->bufferSize() > 7) {
			awaystatus.clear();
			awaystatus = buffer->getBuffer().substr(5, buffer->bufferSize());
			Away = true;
			code = 306;
	}
	return (true);
}

std::string &Client::getID()
{
    return (identifier);
}

bool Client::getAway()
{
	return (Away);
}

std::string  const & Client::getAwayStatus()
{
	return (awaystatus);
}

bool Client::readyForReserve() {
    return (!buffer->reserveIsEmpty());
}

void Client::addChannel(std::string const &name, Channel *c) {
    mychannels[name] = c;
}

bool Client::nickIsAcceptable() {
    size_t length = tempNickname.size();
    if (!length) {
        code = 431;
        return (false);
    }
    char c;
    for (size_t i = 0; i < length; i++) {
        c = tempNickname[i];
        if (!((c >= 34 && c <= 36) || (c >= 38 && c <= 41) || (c >= 43 && c <= 57) || (c >= 60 && c <= 63) || (c >= 65 && c <= 125))) {
            code = 432;
            return (false);
        }
    }
    return (true);
}

bool Client::updateTopic() {
    size_t pos1 = 6;
    size_t pos2 = buffer->getBuffer().find(" :", pos1);
    bool needSetTopic = true;
    if (pos2 == std::string::npos) {
        needSetTopic = false;
        pos2 = buffer->getBuffer().find("\r\n", pos1);
        if (pos2 == std::string::npos)
            return (false);
    }
    std::string channelName = buffer->getBuffer().substr(pos1, pos2 - pos1);
    channel = server->getChannelsList()->find(channelName);
    if (channel == server->getChannelsList()->end())
        return (false);
    if (needSetTopic) {
        if (!(*channel).second->isOperator(nickname))
            return (false);
        pos1 = buffer->getBuffer().find("\r\n", pos2);
        if (pos1 == std::string::npos)
            return (false);
        pos2 += 2;
        (*channel).second->setTopic(buffer->getBuffer().substr(pos2, pos1 - pos2));
        code = 334;
        targetToChannel = true;
        return (true);
    }
    if ((*channel).second->getTopic().empty())
        code = 331;
    else
        code = 332;
    needNoChain = true;
    return (true);
}

bool Client::handleMode() {
    size_t pos1 = 5;
    if (!buffer->charMatches(pos1, '#'))
        return (false);
    bool add = true;
    int handleCase = 0;
    size_t pos2 = buffer->getBuffer().find(" +", pos1);
    if (pos2 == std::string::npos) {
        pos2 = buffer->getBuffer().find(" -", pos1);
        if (pos2 == std::string::npos)
            return (false);
        add = false;
    }
    std::string channelName = buffer->getBuffer().substr(pos1, pos2 - pos1);
    channel = server->getChannelsList()->find(channelName);
    if (channel == server->getChannelsList()->end() || !(*channel).second->isOperator(nickname))
        return (false);
    pos2 += 2;
    if (buffer->charMatches(pos2, 'o') && buffer->charMatches(pos2 + 1, ' ')) {
        pos2 += 2;
        handleCase = 1;
    } else if (buffer->charMatches(pos2, 'b') && (buffer->charMatches(pos2 + 1, ' ')
	|| buffer->charMatches(pos2 + 1, '\r'))) {
        pos2 += 2;
        handleCase = 2;
    } else if (buffer->charMatches(pos2, 'k') && buffer->charMatches(pos2 + 1, ' ')) {
        pos2 += 2;
        handleCase = 3;
    }
    

    pos1 = buffer->getBuffer().find("\r\n", 0);
    if (pos1 == std::string::npos)
        return (false);
    std::string target = buffer->getBuffer().substr(pos2, pos1 - pos2);
    switch (handleCase) {
        case 1: {
            if (!(*channel).second->operatorRequest(target, add))
                return (false);
            code = 330;
            return (true);
        }
        case 2: {
            if ((*channel).second->isOperator(target))
                return (false);
            code = 330;
            return (executeBan(add));
        }
        case 3: {
            if (!add) {
                if (!(*channel).second->isPasswordMatched(target))
                    return (false);
                (*channel).second->setPassword("");
            } else
                (*channel).second->setPassword(target);
            code = 330;
            return (true);
        }
            
    }
    return (false);
}

std::string const Client::getChannelUsersList() const{
    std::string result;
    std::map<std::string, Client *>::iterator it = (*channel).second->getUsers()->begin();
    while (it != (*channel).second->getUsers()->end()) {
        if ((*channel).second->isOperator((*it).first))
            result += "@";
        result += ((*it).second->getNick() + " ");
        it++;
    }
    result += "\n\r\n\n";
    return (result);
}

bool Client::executeKick() {
    size_t pos1 = 5;
    size_t pos2 = buffer->getBuffer().find("\r\n", pos1);
    if (pos2 == std::string::npos)
        return (false);
    std::string channelName, reason;
    size_t pos3 = buffer->getBuffer().find(" :", pos1);
    if (pos3 != std::string::npos) {
        reason = buffer->getBuffer().substr(pos3 + 2, pos2 - pos3 - 2);
        pos2 = pos3;
    } else
        reason = "";
    size_t pos4 = buffer->getBuffer().find(" ", pos1);
    if (pos4 != std::string::npos && pos4 != pos3) {
        victimName = buffer->getBuffer().substr(pos4 + 1, pos2 - pos4 - 1);
        pos2 = pos4;
    } else
        return (false);
    
    channelName = buffer->getBuffer().substr(pos1, pos2 - pos1);
    if (channelName[0] != '#' || !(*channel).second->isOperator(nickname))
        return (false);
    channel = server->getChannelsList()->find(channelName);
    std::map<std::string, Client *>::iterator it = (*channel).second->getUsers()->find(victimName);
    if (it == (*channel).second->getUsers()->end() || (*channel).second->isOperator(victimName))
        return (false);
    code = 336;
    return (true);
}

bool Client::executeBan(bool add) {
    size_t pos1 = 5;
    size_t pos2 = buffer->getBuffer().find("\r\n", pos1);
    if (pos2 == std::string::npos)
        return (false);
    std::string channelName, target;
	size_t pos3 = buffer->getBuffer().rfind(" ", pos2);
	if (buffer->getBuffer()[pos3 + 1] != '+' && buffer->getBuffer()[pos3 + 1] != '-'
	&& buffer->getBuffer()[pos3 - 2] != '+' && buffer->getBuffer()[pos3 - 2] != '-')
		return (false);
	if (buffer->getBuffer()[pos3 + 1] == '+' || buffer->getBuffer()[pos3 + 1] == '-' || pos3 == (pos2 - 1))
		target = "";
	else
		target = buffer->getBuffer().substr(pos3 + 1, pos2 - pos3 - 1);
	size_t pos4 = buffer->getBuffer().find("+", pos1);
	if (pos4 == std::string::npos)
		pos4 = buffer->getBuffer().find("-", pos1);
    channelName = buffer->getBuffer().substr(pos1, pos4 - 1 - pos1);
    if (channelName[0] != '#' || !(*channel).second->isOperator(nickname))
        return (false);
    if (target.empty())
		return (printBanList(channelName, add));
    else {
        channel = server->getChannelsList()->find(channelName);
        (*channel).second->handleBan(add, target);
    }
    return (true);
}

std::string const Client::checkForPassword() const {
	size_t pos1 = 0, pos2 = 0;
	pos1 = buffer->getBuffer().find("PASS ");
	if (pos1 == std::string::npos)
		return ("");
	pos1 += 5;
	pos2 = buffer->getBuffer().find("\r\n", pos1);
	if (pos2 == std::string::npos)
		return ("");
	return (buffer->getBuffer().substr(pos1, pos2 - pos1));
}

bool Client::handleNickChange() {
    tempNickname.clear();
    bufferNick();
    if (tempNickname == nickname)
        return (false);
    if (nickIsAcceptable()) {
        if (allusers->count(tempNickname))
            code = 433;
        else {
            std::map<std::string, Channel *>::iterator it = mychannels.begin();
            while (it != mychannels.end())
            {
                (*it).second->swapUser(nickname, tempNickname, this);
                channel = it;
                it++;
            }
            std::swap(nickname, tempNickname);
            allusers->erase(tempNickname);
            allusers->insert(std::make_pair(nickname, this));
            tempNickname.clear();
            isAuthorized = true;
            code = 337;
        }
    }
    return (true);
}

void Client::broadcastToAllChannels() {
    channel = mychannels.begin();
    std::map<std::string, Client *>::iterator it;
    while (channel != mychannels.end()) {
        it = (*channel).second->getUsers()->begin();
        while (it != (*channel).second->getUsers()->end()) {
            if ((*it).second != this)
                (*it).second->outerRefillBuffer(buffer->getMessage());
            it++;
        }
        channel++;
    }
    outerRefillBuffer(buffer->getMessage());
}

bool Client::printBanList(std::string channelName, bool add)
{
	channel = server->getChannelsList()->find(channelName);
	std::set<std::string> banlist = (*channel).second->getBanlist();
	std::set<std::string>::iterator start = banlist.begin();
	std::set<std::string>::iterator end = banlist.end();
	if (add)
	{
		while (start != end)
		{
			conctd.append((*start));
			conctd.push_back(' ');
			++start;
		}
		code = 367;
	}
    return true;
}

bool Client::handleInvite()
{
	size_t pos1 = 7;
	size_t pos2 = buffer->getBuffer().find("\r\n", pos1);
	if (pos2 == std::string::npos)
		return (false);
	size_t pos3 = buffer->getBuffer().find(" ", pos1);
	if (pos3 == std::string::npos)
		return (false);
	std::string channelName;
	target_nickname = buffer->getBuffer().substr(pos1, pos3 - pos1);
	channelName = buffer->getBuffer().substr(pos3 + 1, pos2 - pos3 - 1);
	Client *target = server->findUser(target_nickname);
	channel = server->getChannelsList()->find(channelName);
	if (channelName[0] != '#')
		return (false);
	if (target == NULL)
	{
		code = 401;
		return (false);
	}
	if (target->mychannels.find(channelName) != target->mychannels.end())
	{
		code = 443;
		return (false);
	}
	privateChat = target;
	if (channel != server->getChannelsList()->end())
	{
		if ((*channel).second->getUsers()->find(nickname) == (*channel).second->getUsers()->end())
		{
			code = 442;
			privateChat = NULL;
			return (false);
		}
		(*channel).second->addUser(target);
		target->mychannels[(*channel).first] = (*channel).second;
	} else {
		server->createChannel(channelName, target);
		channel = server->getChannelsList()->find(channelName);
		(*channel).second->setPassword("");
		(*channel).second->setTopic("");
	}
	code = 341;
	return (true);
}
