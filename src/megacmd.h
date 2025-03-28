/**
 * @file src/megacmd.h
 * @brief MEGAcmd: Interactive CLI and service application
 *
 * (c) 2013 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGAcmd.
 *
 * MEGAcmd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#ifndef MEGACMD_H
#define MEGACMD_H

#include <iostream>
#include <iomanip>
#ifdef _WIN32
#include <algorithm>
#endif
using std::cout;
using std::endl;
using std::max;
using std::min;
using std::flush;
using std::left;
using std::cerr;
using std::istringstream;
using std::locale;
using std::stringstream;
using std::exception;

#include "megacmdcommonutils.h"

#include "megaapi_impl.h"
#include "megacmd_events.h"

#define PROGRESS_COMPLETE -2
namespace megacmd {

typedef struct sync_struct
{
    mega::MegaHandle handle;
    bool active;
    std::string localpath;
    long long fingerprint;
    bool loadedok; //ephimeral data
} sync_struct;


typedef struct backup_struct
{
    mega::MegaHandle handle;
    bool active;
    std::string localpath; //TODO: review wether this is local or utf-8 representation and be consistent
    int64_t period;
    std::string speriod;
    int numBackups;
    bool failed; //This should mark the failure upon resuming. It shall not be persisted
    int tag; //This is depends on execution. should not be persisted
    int id; //Internal id for megacmd. Depends on execution should not be persisted
} backup_istruct;


enum prompttype
{
    COMMAND, LOGINPASSWORD, NEWPASSWORD, PASSWORDCONFIRM, AREYOUSURETODELETE
};

static const char* const prompts[] =
{
    "MEGA CMD> ", "Password:", "New Password:", "Retype New Password:", "Are you sure to delete? "
};

void changeprompt(const char *newprompt);

void informStateListener(std::string message, int clientID);
void broadcastMessage(std::string message, bool keepIfNoListeners = false);
void informStateListeners(std::string s);


void removeDelayedBroadcastMatching(const std::string &toMatch);
void broadcastDelayedMessage(std::string message, bool keepIfNoListeners);

void appendGreetingStatusFirstListener(const std::string &msj);
void removeGreetingStatusFirstListener(const std::string &msj);
void appendGreetingStatusAllListener(const std::string &msj);
void clearGreetingStatusAllListener();
void clearGreetingStatusFirstListener();
void removeGreetingStatusAllListener(const std::string &msj);
void removeGreetingMatching(const std::string &toMatch);
void removeDelayedBroadcastMatching(const std::string &toMatch);

void setloginInAtStartup(bool value);
void setBlocked(int value);
int getBlocked();
void unblock();
bool getloginInAtStartup();
void updatevalidCommands();
void reset();

/**
 * @brief A class to ensure clients are properly informed of login in situations
 */
class LoginGuard {
public:
    LoginGuard()
    {
        appendGreetingStatusAllListener(std::string("login:"));
        setloginInAtStartup(true);
    }

    ~LoginGuard()
    {
        removeGreetingStatusAllListener(std::string("login:"));
        informStateListeners("loged:"); //send this even when failed!
        setloginInAtStartup(false);
    }
};


mega::MegaApi* getFreeApiFolder();
void freeApiFolder(mega::MegaApi *apiFolder);

struct HelpFlags
{
    bool win = false;
    bool apple = false;
    bool usePcre = false;
    bool haveLibuv = false;
    bool readline = true;
    bool fuse = false;
    bool showAll = false;

    HelpFlags(bool showAll = false) :
        showAll(showAll)
    {
#ifdef USE_PCRE
        usePcre = true;
#endif
#ifdef _WIN32
        win = true;
#endif
#ifdef __APPLE__
        apple = true;
#endif
#ifdef HAVE_LIBUV
        haveLibuv = true;
#endif
#ifdef NO_READLINE
        readline = false;
#endif
#ifdef WITH_FUSE
        fuse = true;
#endif
    }
};

const char * getUsageStr(const char *command, const HelpFlags& flags = {});

void unescapeifRequired(std::string &what);

void setprompt(prompttype p, std::string arg = "");

prompttype getprompt();

void printHistory();

int askforConfirmation(std::string message);
bool booleanAskForConfirmation(std::string messageHeading);

std::string askforUserResponse(std::string message);

void* checkForUpdates(void *param);

void stopcheckingForUpdates();
void startcheckingForUpdates();

/**
 * @brief synchronously request the API for a a new version of MEGAcmd
 * It will skip the check if already checked in the past 5 minutes
 * @param api
 * @return a string with a msg with the announcement if a new version is available
 */
std::optional<std::string> lookForAvailableNewerVersions(::mega::MegaApi *api);

void informTransferUpdate(mega::MegaTransfer *transfer, int clientID);
void informStateListenerByClientId(int clientID, std::string s);
void informProgressUpdate(long long transferred, long long total, int clientID, std::string title = "");

void sendEvent(StatsManager::MegacmdEvent event, mega::MegaApi *megaApi, bool wait = true);
void sendEvent(StatsManager::MegacmdEvent event, const char *msg, mega::MegaApi *megaApi, bool wait = true);

#ifdef _WIN32
void uninstall();
#endif

struct LogConfig
{
    int mSdkLogLevel = mega::MegaApi::LOG_LEVEL_DEBUG;
    int mCmdLogLevel = mega::MegaApi::LOG_LEVEL_DEBUG;
    bool mLogToCout = true;
    bool mJsonLogs = false;
};

class LoggedStream; // forward delaration
int executeServer(int argc, char* argv[],
                  const std::function<LoggedStream*()>& createLoggedStream = nullptr,
                  const LogConfig& logConfig = {},
                  bool skiplockcheck = false,
                  std::string debug_api_url = {},
                  bool disablepkp/*only for debugging*/ = false);
void stopServer();

}//end namespace
#endif
