#include <fstream>
#include <cstdio>
#include <ctime>
#include "S2Daemon.h"
#include "Savage.h"
#include "CmdProcessor.h"
#include "CnC.h"
#include "util.h"
#include "Config.h"

S2Daemon* g_pDaemon = NULL;
std::ofstream logfile;

// http://stackoverflow.com/questions/997946/how-to-get-current-time-and-date-in-c
// Get current date/time, format is YYYY-MM-DD.HH:mm:ss
const std::string currentDateTime() {
    time_t     now = time(0);
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);
    // Visit http://en.cppreference.com/w/cpp/chrono/c/strftime
    // for more information about date/time format
    strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);
    return buf;
}

void log(std::string str) {
	logfile.open("/tmp/s2hooker/s2n.log", std::ios_base::app);
	logfile << currentDateTime() << ": " << str;
	logfile.close();
}

static char* makeBitString(uint8_t value, char* buffer)
{
    for(int i = 0; i < 8; i++)
    {
        buffer[7-i] = (value>>i & 1) ? '1' : '0';
    }
    buffer[8] = '\0';
    return buffer;
}

static void dump(uint8_t* buf, size_t len)
{
    static const size_t kBytesPerRow = 16;

    for(size_t i = 0; i < len; i += kBytesPerRow)
    {
        for(size_t j = 0; j < kBytesPerRow && (i+j) < len; j++)
        {
            printf("%02X ", buf[i+j]);
        }
        printf("  ");
        for(size_t j = 0; j < kBytesPerRow && (i+j) < len; j++)
        {
            printf("%c", buf[i+j] >= ' ' && buf[i+j] <= '~' ? buf[i+j] : '.');
        }

        printf("\r\n");
    }
}

static void sdump(char *str, uint8_t* buf, size_t len)
{
    static const size_t kBytesPerRow = 16;
    char tmp[128];
    strcat(str, "\n");
    for(size_t i = 0; i < len; i += kBytesPerRow)
    {
        for(size_t j = 0; j < kBytesPerRow && (i+j) < len; j++)
        {
            sprintf(tmp, "%02X ", buf[i+j]);
            strcat(str, tmp);
        }
        strcat(str, "  ");
        for(size_t j = 0; j < kBytesPerRow && (i+j) < len; j++)
        {
            sprintf(tmp, "%c", buf[i+j] >= ' ' && buf[i+j] <= '~' ? buf[i+j] : '.');
            strcat(str, tmp);
        }
 
        strcat(str, "\r\n");
    }
}


S2Daemon::S2Daemon(void)
{
    Savage::Execute("echo S2Daemon: initializing CmdProcessor");
    CmdProcessor::init();
    Savage::Execute("echo S2Daemon: initializing CnC");
    CnC::init();
    Savage::Execute("echo S2Daemon: reading config");
    char wd[256];
    getcwd(wd, 255);
    std::string cwdMsg = "echo S2Daemon: current working directory is: ";
    cwdMsg += wd;
    Savage::Execute(cwdMsg);
    readConfig();
}
S2Daemon::~S2Daemon(void)
{
}

size_t S2Daemon::OnSendPacket(uint8_t* buf, size_t len)
{
/*
    if(len >= 8)
    {
        Savage::S2Packet pkt(buf, len);
        if(pkt.CmdId() == 0x5B)
        {
            printf("Sent cmd 0x%02X LEN = %zu;\r\n", pkt.CmdId(), pkt.Length());
            char bits[17] = {};
            makeBitString(buf[30], bits);
            makeBitString(buf[31], &bits[8]);
            printf("CTRL %s\r\n", bits);
            dump(&buf[8], len-8);
            printf("-----\r\n");
        }
    }
*/
    return len;
}

size_t S2Daemon::OnReceivePacket(uint8_t* buf, size_t len)
{
//select build ability (human builder)...
//0b 00 00 00 03 80 16 c8 4b 75 00
	if(len < 10) return len;

	if(*(uint32_t *)buf == 0xf197de9a && len > 32) {
		if(!strncmp((char *)&buf[8], "S2_K2_CONNECT", 13)) {
			uint32_t connId = *(uint32_t *)&buf[5] & 0x0ffff;
			unsigned int i = 7;
			while((*(uint32_t *)&buf[i] & 0x0ffff) != connId && i < len - 5) ++i;
			uint32_t accountId = *(uint32_t *)&buf[i+2];
			if(CmdProcessor::disableBuildConnIdSet.find(connId) != CmdProcessor::disableBuildConnIdSet.end())
				CmdProcessor::disableBuildConnIdSet.erase(connId);

			if(CmdProcessor::disableBuildAccountIdSet.find(accountId) 
				!= CmdProcessor::disableBuildAccountIdSet.end())
				CmdProcessor::disableBuildConnIdSet.insert(std::pair<uint32_t, uint32_t>(connId, connId));

			if(CmdProcessor::userMap.find(connId) != CmdProcessor::userMap.end())
				CmdProcessor::userMap.erase(connId);
			CmdProcessor::userMap.insert(std::pair<uint32_t, User*>(connId, new User(accountId)));
			CmdProcessor::acc2connMap.insert(std::pair<uint32_t, uint32_t>(accountId, connId));

			char* str = new char[len * 8];
			str[0] = 0;
			sdump(str, buf, len);
			log(str);
			delete str;

			return len;
		}
	}
	uint32_t connId = *(uint32_t *)&buf[5] & 0x0ffff;

	// log command-type packets for users banned from building
	if(*(uint32_t *)buf != 0xf197de9a && CmdProcessor::disableBuildConnIdSet.find(connId) != CmdProcessor::disableBuildConnIdSet.end()) {
//	if(*(uint32_t *)buf != 0xf197de9a) {
		char* str = new char[len * 8];
		str[0] = 0;
		sdump(str, buf, len);
		log(str);
		delete str;
	}

	if(buf[4] == 3 && buf[7] == 0x0c8) {

		// spam check (All/Team/Squad Chat)
		if(buf[8] > 2 && buf[8] < 6) {
			std::map<unsigned int, User*>::iterator it = CmdProcessor::userMap.find(connId);
			User* user = it->second;
			int accountID = user->accountID;
			int clientNum = SHostServer::GetClientNumFromAccountID(accountID);
			char chatMsg[150] = {0};
			strncpy(chatMsg, (const char *)&buf[9], 90);
			std::string strChat(chatMsg);
			//spam check
			if(len > 100) {
				if(!user->addChatMsg(chatMsg, len - 9)) {
					//duplicate message, kick user
					char str[50] = {0};
					sprintf(str, "kick %d \"Kicked for spamming\"", clientNum);
					std::string cmd = str;
					Savage::Execute(cmd);
					return 0;
				}
			} else {
				if(CmdProcessor::adminSet.find(accountID) != CmdProcessor::adminSet.end()) {
					// admin user sent chat message - check if it's an admin command
					if(strChat.length() > 15) {
						if(!strChat.compare(0, 14, "admin nobuild ")) {
							std::string userID = strChat.substr(14);
							unsigned int accID = atoi(userID.c_str());
							if(CmdProcessor::disableBuildAccountIdSet.find(accID)
								== CmdProcessor::disableBuildAccountIdSet.end()) {
								std::map<uint32_t, uint32_t>::iterator it = CmdProcessor::acc2connMap.find(accID);
								if(it != CmdProcessor::acc2connMap.end())
									CmdProcessor::disableBuildConnIdSet.insert(std::pair<unsigned int, unsigned int>(it->second, it->second));
								CmdProcessor::disableBuildAccountIdSet.insert(std::pair<unsigned int, std::string>(accID, "nickname"));
								updateConfig();
								buf[9] = 0;
								buf[8] = 0; return len;
								//return 10;
							}
						}
						else if(!strChat.compare(0, 12, "admin build ")) {
							std::string userID = strChat.substr(12);
							unsigned int accID = atoi(userID.c_str());
							if(CmdProcessor::disableBuildAccountIdSet.find(accID)
								!= CmdProcessor::disableBuildAccountIdSet.end()) {
								std::map<uint32_t, uint32_t>::iterator it = CmdProcessor::acc2connMap.find(accID);
								if(it != CmdProcessor::acc2connMap.end())
									CmdProcessor::disableBuildConnIdSet.erase(it->second);
								CmdProcessor::disableBuildAccountIdSet.erase(accID);
								updateConfig();
								buf[9] = 0;
								buf[8] = 0; return len;
								//return 10;
							}
						}
					}
				}

				if(!strChat.compare(0, 10, "spawncrate")) {
					char str[512] = {0};
					sprintf(str, "spawnentityatentity #GetIndexFromClientNum(%d)# Npc_Critter definition /npcs/crate/crate.npc", 
						clientNum);
					std::string cmd = str;
					Savage::Execute(cmd);
					buf[8] = 0; buf[9] = 0;
					return len;
				}
			}
		}

		switch(buf[8]) {
			case 3:
			//AllChat packet

			//limit game AllChat message length; game server cannot properly handle messages larger than 1250 bytes
			if(len > 250) {
				buf[250] = 0;
				return 251;
			}
			break;

			case 4:
			//TeamChat packet
			break;

			case 5:
			//SquadChat packet
			break;

			case 75: //0x4b
			//build ability selected
			if(CmdProcessor::disableBuildConnIdSet.find(connId) != CmdProcessor::disableBuildConnIdSet.end()) {
				//Connection Id found on blacklist, disable build ability
				buf[8] = 0;
				std::map<uint32_t, User*>::iterator it = CmdProcessor::userMap.find(connId);
				if(it != CmdProcessor::userMap.end()) {
					char str[250];
					sprintf(str, "Banned build request from banned player with account id = %d\n", it->second->accountID);
					log(str);
				} else {
					// shouldn't happen
					log("Build ban connection ID exists in ban list but doesn't have a user entry!\n");
				}
			}
			return len;

			case 94: //0x5e
			//Call vote

			switch(buf[9]) {
				case 2:
				//shuffle
				break;
				case 3:
				//next map
				break;
			}
			break;
			
		}
	}


/*
    if(len != 13) return len;
    if(buf[4] != 3 || buf[7] != 0x0c8 || buf[8] != 0x5e) return len;
printf("Received next map vote packet!!!!\n");
    buf[9] = 0;
    return len;
*/
/*
    if(len >= 8)
    {
        Savage::S2Packet pkt(buf, len);
        switch(pkt.CmdId())
        {
            case Savage::ekClientSnapshot:
            {
                //printf("Received client snapshot 0x%02X\r\n", pkt.CmdId());
                //dump(buf, len);
                //printf("-----\r\n");
                
            } break;
            case Savage::ekGameData:
            {
                //printf("GAMEDATA PACKET\r\n-----\r\n");
                //dump(buf+7, len-7);
                //printf("-----\r\n");
            } break;
        }

        return pkt.Length();
    }
*/

    //if(pkt.Length() >= 8)// && pkt.ReadDWord() == Savage::kS2Magic)
    //{
    //    uint32_t seq = pkt.ReadDWord();
    //    pkt.ReadByte(); /* Packet type */
    //    uint8_t uid = pkt.ReadWord();
    //    uint8_t cmd = pkt.ReadByte();
    //    //if(cmd != Savage::ekClientSnapshot)
    //    //{
    //        printf("<< cmd 0x%02X\r\n", cmd);
    //        dump(buf, len);
    //        printf("-----\r\n");
    //    //}
    //    switch(cmd)
    //    {
    //        case Savage::ekClientSnapshot:
    //        {
    //            //printf("Received client snapshot from %X...\r\n", uid);
    //        } break;
    //        case Savage::ekGameData:
    //        {
    //            printf("GAMEDATA PACKET\r\n-----\r\n");
    //            dump(buf, len);
    //            printf("-----\r\n");
    //        } break;
    //        case Savage::ekConnect:
    //        {
    //            printf("CONNECT PACKET\r\n-----\r\n");
    //            dump(buf, len);
    //            printf("-----\r\n");
    //        } break;
    //    };
    //}

    return len;
}
