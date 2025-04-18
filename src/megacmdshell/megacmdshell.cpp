/**
 * @file src/megacmdshell.cpp
 * @brief MEGAcmd: Interactive CLI and service application
 * This is the shell application
 *
 * (c) 2013 by Mega Limited, Auckland, New Zealand
 *
 * This file is distributed under the terms of the GNU General Public
 * License, see http://www.gnu.org/copyleft/gpl.txt
 * for details.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "megacmdshell.h"
#include "megacmdshellcommunications.h"
#include "megacmdshellcommunicationsnamedpipes.h"
#include "../megacmdcommonutils.h"

#define USE_VARARGS
#define PREFER_STDARG

#ifdef NO_READLINE
#include <megaconsole.h>
#else
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include <iomanip>
#include <string>
#include <cstring>
#include <mutex>
#include <condition_variable>
#include <set>
#include <map>
#include <vector>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <stdio.h>
#include <future>

#define PROGRESS_COMPLETE -2
#define SPROGRESS_COMPLETE "-2"
#define PROMPT_MAX_SIZE 128

#ifndef _WIN32
#include <signal.h>
#include <sys/types.h>
#include <errno.h>
#else
  #define strdup _strdup
#endif

#define SSTR( x ) static_cast< const std::ostringstream & >( \
        (  std::ostringstream() << std::dec << x ) ).str()

using namespace mega;

namespace megacmd {
using namespace std;

#if defined(NO_READLINE) && defined(_WIN32)
CONSOLE_CLASS* console = NULL;
#endif


// utility functions
#ifndef NO_READLINE
string getCurrentLine()
{
    char *saved_line = rl_copy_text(0, rl_point);
    string toret(saved_line);
    free(saved_line);
    saved_line = NULL;
    return toret;
}
#endif


// end utily functions

string clientID; //identifier for a registered state listener

// Console related functions:
void console_readpwchar(char* pw_buf, int pw_buf_size, int* pw_buf_pos, char** line)
{
#ifdef _WIN32
    char c;
      DWORD cread;

      if (ReadConsole(GetStdHandle(STD_INPUT_HANDLE), &c, 1, &cread, NULL) == 1)
      {
          if ((c == 8) && *pw_buf_pos)
          {
              (*pw_buf_pos)--;
          }
          else if (c == 13)
          {
              *line = (char*)malloc(*pw_buf_pos + 1);
              memcpy(*line, pw_buf, *pw_buf_pos);
              (*line)[*pw_buf_pos] = 0;
          }
          else if (*pw_buf_pos < pw_buf_size)
          {
              pw_buf[(*pw_buf_pos)++] = c;
          }
      }
#else
    // FIXME: UTF-8 compatibility

    char c;

    if (read(STDIN_FILENO, &c, 1) == 1)
    {
        if (c == 8 && *pw_buf_pos)
        {
            (*pw_buf_pos)--;
        }
        else if (c == 13)
        {
            *line = (char*) malloc(*pw_buf_pos + 1);
            memcpy(*line, pw_buf, *pw_buf_pos);
            (*line)[*pw_buf_pos] = 0;
        }
        else if (*pw_buf_pos < pw_buf_size)
        {
            pw_buf[(*pw_buf_pos)++] = c;
        }
    }
#endif
}
void console_setecho(bool echo)
{
#ifdef _WIN32
    HANDLE hCon = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;

    GetConsoleMode(hCon, &mode);

    if (echo)
    {
        mode |= ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT;
    }
    else
    {
        mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    }

    SetConsoleMode(hCon, mode);
#else
    //do nth
#endif
}

std::atomic_bool alreadyFinished = false; //flag to show progress
std::atomic<float> percentDowloaded = 0.0; // to show progress

// password change-related state information
string oldpasswd;
string newpasswd;

bool doExit = false;
bool doReboot = false;
static std::atomic_bool handlerOverridenByExternalThread(false);
static std::mutex handlerInstallerMutex;

static std::atomic_bool requirepromptinstall(true);

std::atomic_bool procesingline = false;
std::atomic_bool promptreinstalledwhenprocessingline = false;
std::atomic_bool serverTryingToLog = false;

static char dynamicprompt[PROMPT_MAX_SIZE];

static char* g_line;

static prompttype prompt = COMMAND;

static char pw_buf[256];
static int pw_buf_pos = 0;

string loginname;
bool signingup = false;
string signupline;
string passwdline;
string linktoconfirm;

bool confirminglink = false;
bool confirmingcancellink = false;

// communications with megacmdserver:
MegaCmdShellCommunications *comms;

std::mutex mutexPrompt;

void printWelcomeMsg(unsigned int width = 0);

#ifndef NO_READLINE
void install_rl_handler(const char *theprompt, bool external = true);
#endif

std::mutex lastMessageMutex;
std::string lastMessage;
bool notRepeatedMessage(const string &newMessage)
{
    std::lock_guard<std::mutex> g(lastMessageMutex);
    bool toret = lastMessage.compare(newMessage);
    if (toret)
    {
        lastMessage = newMessage;
    }
    return toret;
}
void cleanLastMessage()
{
    std::lock_guard<std::mutex> g(lastMessageMutex);
    lastMessage = string();
}

void statechangehandle(string statestring, MegaCmdShellCommunications &comsManager)
{
    char statedelim[2]={(char)0x1F,'\0'};
    size_t nextstatedelimitpos = statestring.find(statedelim);
    static bool shown_partial_progress = false;
    bool promtpReceivedBool = false;

    unsigned int width = getNumberOfCols(75);
    if (width > 1 ) width--;

    while (nextstatedelimitpos!=string::npos && statestring.size())
    {
        string newstate = statestring.substr(0,nextstatedelimitpos);
        statestring=statestring.substr(nextstatedelimitpos+1);
        nextstatedelimitpos = statestring.find(statedelim);
        if (newstate.compare(0, strlen("prompt:"), "prompt:") == 0)
        {
            if (serverTryingToLog)
            {
                printCenteredContentsCerr(string("MEGAcmd Server is still trying to log in. Still, some commands are available.\n"
                             "Type \"help\", to list them.").c_str(), width);
            }
            changeprompt(newstate.substr(strlen("prompt:")).c_str(),true);

            comsManager.markServerReady();
            promtpReceivedBool = true;
        }
        else if (newstate.compare(0, strlen("endtransfer:"), "endtransfer:") == 0)
        {
            string rest = newstate.substr(strlen("endtransfer:"));
            if (rest.size() >=3)
            {
                bool isdown = rest.at(0) == 'D';
                string path = rest.substr(2);
                stringstream os;
                if (shown_partial_progress)
                {
                    os << endl;
                }
                os << (isdown?"Download":"Upload") << " finished: " << path << endl;

#ifdef _WIN32
                wstring wbuffer;
                stringtolocalw((const char*)os.str().data(),&wbuffer);
                WindowsUtf8StdoutGuard utf8Guard;
                OUTSTREAM << wbuffer << flush;
#else
                OUTSTREAM << os.str();
#endif
            }
        }
        else if (newstate.compare(0, strlen("loged:"), "loged:") == 0)
        {
            serverTryingToLog = false;
        }
        else if (newstate.compare(0, strlen("login:"), "login:") == 0)
        {
            serverTryingToLog = true;

            printCenteredContentsCerr(string("Resuming session ... ").c_str(), width, false);
        }
        else if (newstate.compare(0, strlen("message:"), "message:") == 0)
        {
            if (notRepeatedMessage(newstate)) //to avoid repeating messages
            {
                std::string_view messageContents = std::string_view(newstate).substr(strlen("message:"));
                string contents(messageContents);
                replaceAll(contents, "%mega-%", "");

#ifdef _WIN32
                WindowsUtf8StdoutGuard utf8Guard;
#else
                StdoutMutexGuard stdoutGuard;
#endif
                if (messageContents.rfind("-----", 0) != 0)
                {
                    if (!procesingline || promptreinstalledwhenprocessingline || shown_partial_progress)
                    {
                        OUTSTREAM << endl;
                    }
                    printCenteredContents(contents, width);
                    requirepromptinstall = true;
#ifndef NO_READLINE
                    if (prompt == COMMAND && promtpReceivedBool)
                    {
                        std::lock_guard<std::mutex> g(mutexPrompt);
                        redisplay_prompt();
                    }
#endif
                }
                else
                {
                    requirepromptinstall = true;
                    OUTSTREAM << endl <<  contents << endl;
                }
            }
        }
        else if (newstate.compare(0, strlen("clientID:"), "clientID:") == 0)
        {
            clientID = newstate.substr(strlen("clientID:")).c_str();
        }
        else if (newstate.compare(0, strlen("progress:"), "progress:") == 0)
        {
            string rest = newstate.substr(strlen("progress:"));

            size_t nexdel = rest.find(":");
            string received = rest.substr(0,nexdel);

            rest = rest.substr(nexdel+1);
            nexdel = rest.find(":");
            string total = rest.substr(0,nexdel);

            string title;
            if ( (nexdel != string::npos) && (nexdel < rest.size() ) )
            {
                rest = rest.substr(nexdel+1);
                nexdel = rest.find(":");
                title = rest.substr(0,nexdel);
            }

            if (received!=SPROGRESS_COMPLETE)
            {
                shown_partial_progress = true;
            }
            else
            {
                shown_partial_progress = false;
            }

            long long completed = received == SPROGRESS_COMPLETE ? PROGRESS_COMPLETE : charstoll(received.c_str());
            const char * progressTitle = title.empty() ? "TRANSFERRING" : title.c_str();
            printprogress(completed, charstoll(total.c_str()), progressTitle);
        }
        else if (newstate == "ack")
        {
            // do nothing, all good
        }
        else if (newstate == "restart")
        {
            doExit = true;
            doReboot = true;
            comsManager.markServerIsUpdating(); // to avoid mensajes about server down

            sleepSeconds(3); // Give a while for server to restart
            changeprompt("RESTART REQUIRED BY SERVER (due to an update). Press any key to continue.", true);
        }
        else
        {
            if (shown_partial_progress)
            {
                OUTSTREAM << endl;
            }
            cerr << "received unrecognized state change: [" << newstate << "]" << endl;
            //sleep a while to avoid continuous looping
            sleepSeconds(1);
        }


        if (newstate.compare(0, strlen("progress:"), "progress:") != 0)
        {
            shown_partial_progress = false;
        }
    }
}


void sigint_handler(int signum)
{
    if (prompt != COMMAND)
    {
        setprompt(COMMAND);
    }

#ifndef NO_READLINE
    // reset position and print prompt
    rl_replace_line("", 0); //clean contents of actual command
    rl_crlf(); //move to nextline

    if (RL_ISSTATE(RL_STATE_ISEARCH) || RL_ISSTATE(RL_STATE_ISEARCH) || RL_ISSTATE(RL_STATE_ISEARCH))
    {
        RL_UNSETSTATE(RL_STATE_ISEARCH);
        RL_UNSETSTATE(RL_STATE_NSEARCH);
        RL_UNSETSTATE( RL_STATE_SEARCH);
        history_set_pos(history_length);
        rl_restore_prompt(); // readline has stored it when searching
    }
    else
    {
        rl_reset_line_state();
    }
    rl_redisplay();
#endif
}

void printprogress(long long completed, long long total, const char *title)
{
    float oldpercent = percentDowloaded;
    if (total == 0)
    {
        percentDowloaded = 0;
    }
    else
    {
        percentDowloaded = float(completed * 1.0 / total * 100.0);
    }
    if (completed != PROGRESS_COMPLETE && (alreadyFinished || ( ( percentDowloaded == oldpercent ) && ( oldpercent != 0 ) ) ))
    {
        return;
    }
    if (percentDowloaded < 0)
    {
        percentDowloaded = 0;
    }

    if (total < 0)
    {
        return; // after a 100% this happens
    }
    if (completed != PROGRESS_COMPLETE  && completed < 0.001 * total)
    {
        return; // after a 100% this happens
    }
    if (completed == PROGRESS_COMPLETE)
    {
        alreadyFinished = true;
        completed = total;
        percentDowloaded = 100;
    }
    printPercentageLineCerr(title, completed, total, percentDowloaded, !alreadyFinished);
}


#ifdef _WIN32
BOOL WINAPI CtrlHandler( DWORD fdwCtrlType )
{
  cerr << "Reached CtrlHandler: " << fdwCtrlType << endl;

  switch( fdwCtrlType )
  {
    // Handle the CTRL-C signal.
    case CTRL_C_EVENT:
       sigint_handler((int)fdwCtrlType);
      return( TRUE );

    default:
      return FALSE;
  }
}
#endif

prompttype getprompt()
{
    return prompt;
}

void setprompt(prompttype p, string arg)
{
    prompt = p;

#ifndef NO_READLINE
    if (p == COMMAND)
    {
        console_setecho(true);
    }
    else
    {
        pw_buf_pos = 0;
        if (arg.size())
        {
            OUTSTREAM << arg << flush;
        }
        else
        {
            OUTSTREAM << prompts[p] << flush;
        }
        console_setecho(false);
    }
#else
    console->setecho(p == COMMAND);

    if (p != COMMAND)
    {
        pw_buf_pos = 0;
        console->updateInputPrompt(arg.empty() ? prompts[p] : arg);
    }
#endif
}

#ifndef NO_READLINE
// readline callback - exit if EOF, add to history unless password
static void store_line(char* l)
{
    procesingline = true;
    if (!l)
    {
#ifndef _WIN32 // to prevent exit with Supr key
        doExit = true;
        rl_set_prompt("(CTRL+D) Exiting ...\n");
#ifndef NDEBUG
        if (comms->mServerInitiatedFromShell)
        {
            OUTSTREAM << " Forwarding exit command to the server, since this cmd shell (most likely) initiated it" << endl;
            comms->executeCommand("exit", readresponse);
        }
#endif
#endif
        return;
    }

    if (*l && ( prompt == COMMAND ))
    {
        add_history(l);
    }

    g_line = l;
}
#endif

#ifdef _WIN32

bool validoptionforreadline(const string& string)
{// TODO: this has not been tested in 100% cases (perhaps it is too diligent or too strict)
    int c,i,ix,n,j;
    for (i=0, ix=int(string.length()); i < ix; i++)
    {
        c = (unsigned char) string[i];

        //if (c>0xC0) return false;
        //if (c==0x09 || c==0x0a || c==0x0d || (0x20 <= c && c <= 0x7e) ) n = 0; // is_printable_ascii
        if (0x00 <= c && c <= 0x7f) n=0; // 0bbbbbbb
        else if ((c & 0xE0) == 0xC0) n=1; // 110bbbbb
        else if ( c==0xed && i<(ix-1) && ((unsigned char)string[i+1] & 0xa0)==0xa0) return false; //U+d800 to U+dfff
        else if ((c & 0xF0) == 0xE0) {return false; /*n=2;*/} // 1110bbbb
        else if ((c & 0xF8) == 0xF0) {return false; /*n=3;*/} // 11110bbb
        //else if (($c & 0xFC) == 0xF8) n=4; // 111110bb //byte 5, unnecessary in 4 byte UTF-8
        //else if (($c & 0xFE) == 0xFC) n=5; // 1111110b //byte 6, unnecessary in 4 byte UTF-8
        else return false;
        for (j=0; j<n && i<ix; j++) { // n bytes matching 10bbbbbb follow ?
            if ((++i == ix) || (( (unsigned char)string[i] & 0xC0) != 0x80))
                return false;
        }
    }
    return true;
}

bool validwcharforeadline(const wchar_t thewchar)
{
    wstring input;
    input+=thewchar;
    string output;
    localwtostring(&input,&output);
    return validoptionforreadline(output);
}

wstring escapereadlinebreakers(const wchar_t *what)
{
    wstring output;
    for( unsigned int i = 0; i < wcslen( what ) ; i++ )
    {
        if(validwcharforeadline(what[ i ] ))
        {
            output.reserve( output.size() + 1 );
            output += what[ i ];
        } else {
#ifndef __MINGW32__
            wchar_t code[ 7 ];
            swprintf( code, 7, L"\\u%0.4X", what[ i ] ); //while this does not work (yet) as what, at least it shows something and does not break
            //TODO: ideally we would do the conversion from escaped unicode chars \uXXXX back to wchar_t in the server
            // NOTICE: I was able to execute a command with a literl \x242ee (which correspond to \uD850\uDEEE in UTF16).
            // So it'll be more interesting to output here the complete unicode char and in unescapeutf16escapedseqs revert it.
            //     or keep here the UTF16 escaped secs and revert them correctly in the unescapeutf16escapedseqs
            output.reserve( output.size() + 7 ); // "\u"(2) + 5(uint max digits capacity)
            output += code;
#endif
        }
    }
    return output;
}
#endif

#ifndef NO_READLINE
void install_rl_handler(const char *theprompt, bool external)
{
    std::lock_guard<std::mutex> lkrlhandler(handlerInstallerMutex);
    if (procesingline)
    {
        promptreinstalledwhenprocessingline = true;
    }

    rl_restore_prompt();
    rl_callback_handler_install(theprompt, store_line);

    handlerOverridenByExternalThread = external;
    requirepromptinstall = false;
}

void redisplay_prompt()
{
    int saved_point = rl_point;
    char *saved_line = rl_copy_text(0, rl_end);

    rl_clear_message();

    // enter a new line if not processing sth (otherwise, the newline should already be there)
    if (!procesingline)
    {
        rl_crlf();
    }

    if (prompt == COMMAND)
    {
        install_rl_handler(*dynamicprompt ? dynamicprompt : prompts[COMMAND]);
    }

    // restore line
    if (saved_line)
    {
        rl_replace_line(saved_line, 0);
        free(saved_line);
        saved_line = NULL;
    }
    rl_point = saved_point;
    rl_redisplay();
}

#endif


void changeprompt(const char *newprompt, bool redisplay)
{
    std::lock_guard<std::mutex> g(mutexPrompt);

    if (*dynamicprompt)
    {
        if (!strcmp(newprompt,dynamicprompt))
            return; //same prompt. do nth
    }

    strncpy(dynamicprompt, newprompt, sizeof( dynamicprompt ));

    if (strlen(newprompt) >= PROMPT_MAX_SIZE)
    {
        strncpy(dynamicprompt, newprompt, PROMPT_MAX_SIZE/2-1);
        dynamicprompt[PROMPT_MAX_SIZE/2-1] = '.';
        dynamicprompt[PROMPT_MAX_SIZE/2] = '.';

        strncpy(dynamicprompt+PROMPT_MAX_SIZE/2+1, newprompt+(strlen(newprompt)-PROMPT_MAX_SIZE/2+2), PROMPT_MAX_SIZE/2-2);
        dynamicprompt[PROMPT_MAX_SIZE-1] = '\0';
    }

#ifdef NO_READLINE
    console->updateInputPrompt(newprompt);
#else
    if (redisplay)
    {
        // save line
        redisplay_prompt();
    }

#endif

    static bool firstime = true;
    if (firstime)
    {
        firstime = false;
#if _WIN32
        if( !SetConsoleCtrlHandler( CtrlHandler, TRUE ) )
        {
            cerr << "Control handler set failed" << endl;
        }
#else
        // prevent CTRL+C exit
        signal(SIGINT, sigint_handler);
#endif
    }
}

void escapeEspace(string &orig)
{
    replaceAll(orig," ", "\\ ");
}

void unescapeEspace(string &orig)
{
    replaceAll(orig,"\\ ", " ");
}

#ifndef NO_READLINE
char* empty_completion(const char* text, int state)
{
    // we offer 2 different options so that it doesn't complete (no space is inserted)
    if (state == 0)
    {
        return strdup(" ");
    }
    if (state == 1)
    {
        return strdup(text);
    }
    return NULL;
}

char* generic_completion(const char* text, int state, vector<string> validOptions)
{
    static size_t list_index, len;
    static bool foundone;
    string name;
    if (!validOptions.size()) // no matches
    {
        return empty_completion(text,state); //dont fall back to filenames
    }
    if (!state)
    {
        list_index = 0;
        foundone = false;
        len = strlen(text);
    }
    while (list_index < validOptions.size())
    {
        name = validOptions.at(list_index);
        if (!rl_completion_quote_character) {
            escapeEspace(name);
        }

        list_index++;

        if (!( strcmp(text, "")) || (( name.size() >= len ) && ( strlen(text) >= len ) && ( name.find(text) == 0 )))
        {
            if (name.size() && (( name.at(name.size() - 1) == '=' ) || ( name.at(name.size() - 1) == '\\' ) || ( name.at(name.size() - 1) == '/' )))
            {
                rl_completion_suppress_append = 1;
            }
            foundone = true;
            return dupstr((char*)name.c_str());
        }
    }

    if (!foundone)
    {
        return empty_completion(text,state); //dont fall back to filenames
    }

    return((char*)NULL );
}
#endif

char* local_completion(const char* text, int state)
{
    return((char*)NULL );  //matches will be NULL: readline will use local completion
}

void pushvalidoption(vector<string>  *validOptions, const char *beginopt)
{
#ifdef _WIN32
    if (validoptionforreadline(beginopt))
    {
        validOptions->push_back(beginopt);
    }
    else
    {
        wstring input;
        stringtolocalw(beginopt,&input);
        wstring output = escapereadlinebreakers(input.c_str());

        string soutput;
        localwtostring(&output,&soutput);
        validOptions->push_back(soutput.c_str());
    }
#else
    validOptions->push_back(beginopt);
#endif
}

void changedir(const string& where)
{
#ifdef _WIN32
    wstring wwhere;
    stringtolocalw(where.c_str(), &wwhere);
    wwhere.append(L"\\");
    int r = SetCurrentDirectoryW((LPCWSTR)wwhere.data());
    if (!r)
    {
        cerr << "Error at SetCurrentDirectoryW before local completion to " << where << ". errno: " << ERRNO << endl;
    }
#else
    chdir(where.c_str());
#endif
}

#ifndef NO_READLINE
char* remote_completion(const char* text, int state)
{
    string saved_line = getCurrentLine();

    static vector<string> validOptions;
    if (state == 0)
    {
        validOptions.clear();
        string completioncommand("completionshell ");
        completioncommand += saved_line;

        OUTSTRING s;
        OUTSTRINGSTREAM oss(s);

        comms->executeCommand(completioncommand, readresponse, oss);

        string outputcommand;

        outputcommand = oss.str();

        if (outputcommand == "MEGACMD_USE_LOCAL_COMPLETION")
        {
            return local_completion(text,state); //fallback to local path completion
        }

        if (outputcommand.find("MEGACMD_USE_LOCAL_COMPLETION") == 0)
        {
            string where = outputcommand.substr(strlen("MEGACMD_USE_LOCAL_COMPLETION"));
            changedir(where);
            return local_completion(text,state); //fallback to local path completion
        }

        char *ptr = (char *)outputcommand.c_str();

        char *beginopt = ptr;
        while (*ptr)
        {
            if (*ptr == 0x1F)
            {
                *ptr = '\0';
                if (strcmp(beginopt," ")) //the server will give a " " for empty_completion (no matches)
                {
                    pushvalidoption(&validOptions,beginopt);
                }

                beginopt=ptr+1;
            }
            ptr++;
        }
        if (*beginopt && strcmp(beginopt," "))
        {
            pushvalidoption(&validOptions,beginopt);
        }
    }
    return generic_completion(text, state, validOptions);
}

static char** getCompletionMatches(const char * text, int start, int end)
{
    rl_filename_quoting_desired = 1;

    char **matches;

    matches = (char**)NULL;

    matches = rl_completion_matches((char*)text, remote_completion);

    return( matches );
}

void printHistory()
{
    int length = history_length;
    int offset = 1;
    int rest = length;
    while (rest >= 10)
    {
        offset++;
        rest = rest / 10;
    }

    for (int i = 0; i < length; i++)
    {
        history_set_pos(i);
        OUTSTREAM << setw(offset) << i << "  " << current_history()->line << endl;
    }
}

void wait_for_input(int readline_fd)
{
    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(readline_fd, &fds);

    int rc = select(FD_SETSIZE, &fds, NULL, NULL, NULL);
    if (rc < 0)
    {
        if (ERRNO != EINTR)  //syscall
        {
            cerr << "Error at select at wait_for_input errno: " << ERRNO << endl;
            return;
        }
    }
}
#else

vector<autocomplete::ACState::Completion> remote_completion(string linetocomplete)
{
    using namespace autocomplete;
    vector<ACState::Completion> result;

    // normalize any partially or intermediately quoted strings, eg.  `put c:\Program" Fi` or `/My" Documents/"`
    ACState acs = prepACState(linetocomplete, linetocomplete.size(), console->getAutocompleteStyle());
    string refactoredline;
    for (auto& s : acs.words)
    {
        refactoredline += (refactoredline.empty() ? "" : " ") + s.getQuoted();
    }

    OUTSTRING s;
    OUTSTRINGSTREAM oss(s);
    comms->executeCommand(string("completionshell ") + refactoredline, readresponse, oss);

    string outputcommand;
    auto ossstr=oss.str();
    localwtostring(&ossstr, &outputcommand);

    ACState::quoted_word completionword = acs.words.size() ? acs.words[acs.words.size() - 1] : string();

    if (outputcommand.find("MEGACMD_USE_LOCAL_COMPLETION") == 0)
    {
        string where;
        bool folders = false;
        if (outputcommand.find("MEGACMD_USE_LOCAL_COMPLETIONFOLDERS") == 0)
        {
            where = outputcommand.substr(strlen("MEGACMD_USE_LOCAL_COMPLETIONFOLDERS"));
            folders = true;
        }
        else
        {
            where = outputcommand.substr(strlen("MEGACMD_USE_LOCAL_COMPLETION"));
        }
        changedir(where);

        if (acs.words.size())
        {
            string l = completionword.getQuoted();
            CompletionState cs = autoComplete(l, l.size(), folders ? localFSFolder() : localFSPath(), console->getAutocompleteStyle());
            result.swap(cs.completions);
        }
        return result;
    }
    else
    {
        char *ptr = (char *)outputcommand.c_str();

        char *beginopt = ptr;
        while (*ptr)
        {
            if (*ptr == 0x1F)
            {
                *ptr = '\0';
                if (strcmp(beginopt, " ")) //the server will give a " " for empty_completion (no matches)
                {
                    result.push_back(autocomplete::ACState::Completion(beginopt, false));
                }

                beginopt = ptr + 1;
            }
            ptr++;
        }
        if (*beginopt && strcmp(beginopt, " "))
        {
            result.push_back(autocomplete::ACState::Completion(beginopt, false));
        }

        if (result.size() == 1 && result[0].s == completionword.s)
        {
            result.clear();  // for parameters it returns the same string when there are no matches
        }
        return result;
    }
}

void exec_clear(autocomplete::ACState& s)
{
    console->clearScreen();
}

void exec_history(autocomplete::ACState& s)
{
    console->outputHistory();
}

void exec_dos_unix(autocomplete::ACState& s)
{
    if (s.words.size() < 2)
    {
        OUTSTREAM << "autocomplete style: " << (console->getAutocompleteStyle() ? "unix" : "dos") << endl;
    }
    else
    {
        console->setAutocompleteStyle(s.words[1].s == "unix");
    }
}

void exec_codepage(autocomplete::ACState& s)
{
    if (s.words.size() == 1)
    {
        UINT cp1, cp2;
        console->getShellCodepages(cp1, cp2);
        cout << "Current codepage is " << cp1;
        if (cp2 != cp1)
        {
            cout << " with failover to codepage " << cp2 << " for any absent glyphs";
        }
        cout << endl;
        for (int i = 32; i < 256; ++i)
        {
            string theCharUtf8 = WinConsole::toUtf8String(WinConsole::toUtf16String(string(1, (char)i), cp1));
            cout << "  dec/" << i << " hex/" << hex << i << dec << ": '" << theCharUtf8 << "'";
            if (i % 4 == 3)
            {
                cout << endl;
            }
        }
    }
    else if (s.words.size() == 2 && atoi(s.words[1].s.c_str()) != 0)
    {
        if (!console->setShellConsole(atoi(s.words[1].s.c_str()), atoi(s.words[1].s.c_str())))
        {
            cout << "Code page change failed - unicode selected" << endl;
        }
    }
    else if (s.words.size() == 3 && atoi(s.words[1].s.c_str()) != 0 && atoi(s.words[2].s.c_str()) != 0)
    {
        if (!console->setShellConsole(atoi(s.words[1].s.c_str()), atoi(s.words[2].s.c_str())))
        {
            cout << "Code page change failed - unicode selected" << endl;
        }
    }
    else
    {
        cout << "      codepage [N [N]]" << endl;
    }
}


autocomplete::ACN autocompleteSyntax;

autocomplete::ACN buildAutocompleteSyntax()
{
    using namespace autocomplete;
    std::unique_ptr<Either> p(new Either("      "));

    p->Add(exec_clear,      sequence(text("clear")));
    p->Add(exec_codepage,   sequence(text("codepage"), opt(sequence(wholenumber(65001), opt(wholenumber(65001))))));
    p->Add(exec_dos_unix,   sequence(text("autocomplete"), opt(either(text("unix"), text("dos")))));
    p->Add(exec_history,    sequence(text("history")));

    return autocompleteSyntax = std::move(p);
}

void printHistory()
{
    console->outputHistory();
}
#endif

bool isserverloggedin()
{
    if (comms->executeCommand(("loggedin")) == MCMD_NOTLOGGEDIN )
    {
        return false;
    }
    return true;
}


void process_line(const char * line)
{
    string refactoredline;

    switch (prompt)
    {
        case AREYOUSURE:
            //this is currently never used
            if (!strcasecmp(line,"yes") || !strcasecmp(line,"y"))
            {
                comms->setResponseConfirmation(true);
                setprompt(COMMAND);
            }
            else if (!strcasecmp(line,"no") || !strcasecmp(line,"n"))
            {
                comms->setResponseConfirmation(false);
                setprompt(COMMAND);
            }
            else
            {
                //Do nth, ask again
                OUTSTREAM << "Please enter: [y]es/[n]o: " << flush;
            }
            break;
        case LOGINPASSWORD:
        {
            if (!strlen(line))
            {
                break;
            }
            if (confirminglink)
            {
                string confirmcommand("confirm ");
                confirmcommand+=linktoconfirm;
                confirmcommand+=" " ;
                confirmcommand+=loginname;
                confirmcommand+=" \"";
                confirmcommand+=line;
                confirmcommand+="\"" ;
                OUTSTREAM << endl;
                comms->executeCommand(confirmcommand.c_str(), readresponse);
            }
            else if (confirmingcancellink)
            {
                string confirmcommand("confirmcancel ");
                confirmcommand+=linktoconfirm;
                confirmcommand+=" \"";
                confirmcommand+=line;
                confirmcommand+="\"" ;
                OUTSTREAM << endl;
                comms->executeCommand(confirmcommand.c_str(), readresponse);
            }
            else
            {
                string logincommand("login -v ");
                if (clientID.size())
                {
                    logincommand += "--clientID=";
                    logincommand+=clientID;
                    logincommand+=" ";
                }
                logincommand+=loginname;
                logincommand+=" \"" ;
                logincommand+=line;
                logincommand+="\"" ;
                OUTSTREAM << endl;
                comms->executeCommand(logincommand.c_str(), readresponse);
            }
            confirminglink = false;
            confirmingcancellink = false;

            setprompt(COMMAND);
            break;
        }
        case NEWPASSWORD:
        {
            if (!strlen(line))
            {
                break;
            }
            newpasswd = line;
            OUTSTREAM << endl;
            setprompt(PASSWORDCONFIRM);
            break;
        }
        case PASSWORDCONFIRM:
        {
            if (!strlen(line))
            {
                break;
            }
            if (line != newpasswd)
            {
                OUTSTREAM << endl << "New passwords differ, please try again" << endl;
            }
            else
            {
                OUTSTREAM << endl;

                if (signingup)
                {
                    signupline += " \"";
                    signupline += newpasswd;
                    signupline += "\"";
                    comms->executeCommand(signupline.c_str(), readresponse);

                    signingup = false;
                }
                else
                {
                    string changepasscommand(passwdline);
                    passwdline = " ";
                    changepasscommand+=" " ;
                    if (oldpasswd.size())
                    {
                        changepasscommand+="\"" ;
                        changepasscommand+=oldpasswd;
                        changepasscommand+="\"" ;
                    }
                    changepasscommand+=" \"" ;
                    changepasscommand+=newpasswd;
                    changepasscommand+="\"" ;
                    comms->executeCommand(changepasscommand.c_str(), readresponse);
                }
            }

            setprompt(COMMAND);
            break;
        }
        case COMMAND:
        {

#ifdef NO_READLINE
            // if local command and syntax is satisfied, execute it
            string consoleOutput;
            if (autocomplete::autoExec(line, string::npos, autocompleteSyntax, false, consoleOutput, false))
            {
                COUT << consoleOutput << flush;
                return;
            }

            // normalize any partially or intermediately quoted strings, eg.  `put c:\Program" Fi` or get `/My" Documents/"`
            autocomplete::ACState acs = autocomplete::prepACState(line, strlen(line), console->getAutocompleteStyle());
            for (auto& s : acs.words)
            {
                refactoredline += (refactoredline.empty() ? "" : " ") + s.getQuoted();
            }
            line = refactoredline.c_str();
#endif

            vector<string> words = getlistOfWords(line);

            string clientWidth = "--client-width=";
            clientWidth+= SSTR(getNumberOfCols(80));

            words.insert(words.begin()+1, clientWidth);

            string scommandtoexec(words[0]);
            scommandtoexec+=" ";
            scommandtoexec+=clientWidth;
            scommandtoexec+=" ";

            if (strlen(line)>(words[0].size()+1))
            {
                scommandtoexec+=line+words[0].size()+1;
            }

            const char *commandtoexec = scommandtoexec.c_str();

            bool helprequested = false;
            for (unsigned int i = 1; i< words.size(); i++)
            {
                if (words[i]== "--help") helprequested = true;
            }
            if (words.size())
            {
                if ( words[0] == "exit" || words[0] == "quit")
                {
                    if (find(words.begin(), words.end(), "--only-shell") == words.end())
                    {
                        if (comms->executeCommand(commandtoexec, readresponse) == MCMD_CONFIRM_NO)
                        {
                            return;
                        }
                    }

                    if (find(words.begin(), words.end(), "--help") == words.end()
                            && find(words.begin(), words.end(), "--only-server") == words.end() )
                    {
                        doExit = true;
                    }
                }
#if defined(_WIN32) || defined(__APPLE__)
                else if (words[0] == "update")
                {
                    comms->markServerIsUpdating();
                    int ret = comms->executeCommand(commandtoexec, readresponse);
                    if (ret == MCMD_REQRESTART)
                    {
                        OUTSTREAM << "MEGAcmd has been updated ... this shell will be restarted before proceding...." << endl;
                        doExit = true;
                        doReboot = true;
                    }
                    else if (ret != MCMD_INVALIDSTATE && words.size() == 1)
                    {
                        comms->unmarkServerIsUpdating();
                    }
                }
#endif
                else if (words[0] == "history")
                {
                    if (helprequested)
                    {
                        OUTSTREAM << " Prints commands history" << endl;
                    }
                    else
                    {
                        printHistory();
                    }
                }
#if defined(_WIN32) && !defined(NO_READLINE)
                else if (!helprequested && words[0] == "unicode" && words.size() == 1)
                {
                    rl_getc_function=(rl_getc_function==&getcharacterreadlineUTF16support)?rl_getc:&getcharacterreadlineUTF16support;
                    OUTSTREAM << "Unicode shell input " << ((rl_getc_function==&getcharacterreadlineUTF16support)?"ENABLED":"DISABLED") << endl;
                    return;
                }
#endif
                else if (!helprequested && words[0] == "passwd")
                {
                    if (isserverloggedin())
                    {
                        passwdline = commandtoexec;
                        discardOptionsAndFlags(&words);
                        if (words.size() == 1)
                        {
                            setprompt(NEWPASSWORD);
                        }
                        else
                        {
                            comms->executeCommand(commandtoexec, readresponse);
                        }
                    }
                    else
                    {
                        cerr << "Not logged in." << endl;
                    }

                    return;
                }
                else if (!helprequested && words[0] == "login")
                {
                    if (!isserverloggedin())
                    {
                        discardOptionsAndFlags(&words);

                        if ( (words.size() == 2 || ( words.size() == 3 && !words[2].size() ) )
                                && (words[1].find("@") != string::npos))
                        {
                            loginname = words[1];
                            setprompt(LOGINPASSWORD);
                        }
                        else
                        {
                            string s = commandtoexec;
                            if (clientID.size())
                            {
                                s = "login --clientID=";
                                s+=clientID;
                                s.append(string(commandtoexec).substr(5));
                            }
                            comms->executeCommand(s, readresponse);
                        }
                    }
                    else
                    {
                        cerr << "Already logged in. Please log out first." << endl;
                    }
                    return;
                }
                else if (!helprequested && words[0] == "signup")
                {
                    if (!isserverloggedin())
                    {
                        signupline = commandtoexec;
                        discardOptionsAndFlags(&words);

                        if (words.size() == 2)
                        {
                            loginname = words[1];
                            signingup = true;
                            setprompt(NEWPASSWORD);
                        }
                        else
                        {
                            comms->executeCommand(commandtoexec, readresponse);
                        }
                    }
                    else
                    {
                        cerr << "Please loggout first." << endl;
                    }
                    return;
                }
                else if (!helprequested && words[0] == "confirm")
                {
                    discardOptionsAndFlags(&words);

                    if (words.size() == 3)
                    {
                        linktoconfirm = words[1];
                        loginname = words[2];
                        confirminglink = true;
                        setprompt(LOGINPASSWORD);
                    }
                    else
                    {
                        comms->executeCommand(commandtoexec, readresponse);
                    }
                }
                else if (!helprequested && words[0] == "confirmcancel")
                {
                    discardOptionsAndFlags(&words);

                    if (words.size() == 2)
                    {
                        linktoconfirm = words[1];
                        confirmingcancellink = true;
                        setprompt(LOGINPASSWORD);
                    }
                    else
                    {
                        comms->executeCommand(commandtoexec, readresponse);
                    }
                    return;
                }
                else if (!helprequested && words[0] == "clear")
                {
#ifdef _WIN32
                    HANDLE hStdOut;
                    CONSOLE_SCREEN_BUFFER_INFO csbi;
                    DWORD count;

                    hStdOut = GetStdHandle( STD_OUTPUT_HANDLE );
                    if (hStdOut == INVALID_HANDLE_VALUE) return;

                    /* Get the number of cells in the current buffer */
                    if (!GetConsoleScreenBufferInfo( hStdOut, &csbi )) return;
                    /* Fill the entire buffer with spaces */
                    if (!FillConsoleOutputCharacter( hStdOut, (TCHAR) ' ', csbi.dwSize.X *csbi.dwSize.Y, { 0, 0 }, &count ))
                    {
                        return;
                    }
                    /* Fill the entire buffer with the current colors and attributes */
                    if (!FillConsoleOutputAttribute(hStdOut, csbi.wAttributes, csbi.dwSize.X *csbi.dwSize.Y, { 0, 0 }, &count))
                    {
                        return;
                    }
                    /* Move the cursor home */
                    SetConsoleCursorPosition( hStdOut, { 0, 0 } );
#elif __linux__
                    printf("\033[H\033[J");
#else
                    rl_clear_screen(0,0);
#endif
                    return;
                }
                else if ( (words[0] == "transfers"))
                {
                    string toexec;

                    if (!strstr (commandtoexec,"path-display-size"))
                    {
                        unsigned int width = getNumberOfCols(75);
                        int pathSize = int((width-46)/2);

                        toexec+=words[0];
                        toexec+=" --path-display-size=";
                        toexec+=SSTR(pathSize);
                        toexec+=" ";
                        if (strlen(commandtoexec)>(words[0].size()+1))
                        {
                            toexec+=commandtoexec+words[0].size()+1;
                        }
                    }
                    else
                    {
                        toexec+=commandtoexec;
                    }

                    comms->executeCommand(toexec.c_str(), readresponse);
                }
                else if ( (words[0] == "du"))
                {
                    string toexec;

                    if (!strstr (commandtoexec,"path-display-size"))
                    {
                        unsigned int width = getNumberOfCols(75);
                        int pathSize = int(width-13);
                        if (strstr(commandtoexec, "--versions"))
                        {
                            pathSize -= 11;
                        }

                        toexec+=words[0];
                        toexec+=" --path-display-size=";
                        toexec+=SSTR(pathSize);
                        toexec+=" ";
                        if (strlen(commandtoexec)>(words[0].size()+1))
                        {
                            toexec+=commandtoexec+words[0].size()+1;
                        }
                    }
                    else
                    {
                        toexec+=commandtoexec;
                    }

                    comms->executeCommand(toexec.c_str(), readresponse);
                }
                else if (words[0] == "sync")
                {
                    string toexec;

                    if (!strstr (commandtoexec,"path-display-size"))
                    {
                        unsigned int width = getNumberOfCols(75);
                        int pathSize = int((width-46)/2);

                        toexec+="sync --path-display-size=";
                        toexec+=SSTR(pathSize);
                        toexec+=" ";
                        if (strlen(commandtoexec)>strlen("sync "))
                        {
                            toexec+=commandtoexec+strlen("sync ");
                        }
                    }
                    else
                    {
                        toexec+=commandtoexec;
                    }

                    comms->executeCommand(toexec.c_str(), readresponse);
                }
                else if (words[0] == "mediainfo")
                {
                    string toexec;

                    if (!strstr (commandtoexec,"path-display-size"))
                    {
                        unsigned int width = getNumberOfCols(75);
                        int pathSize = int(width - 28);

                        toexec+=words[0];
                        toexec+=" --path-display-size=";
                        toexec+=SSTR(pathSize);
                        toexec+=" ";
                        if (strlen(commandtoexec)>(words[0].size()+1))
                        {
                            toexec+=commandtoexec+words[0].size()+1;
                        }
                    }
                    else
                    {
                        toexec+=commandtoexec;
                    }

                    comms->executeCommand(toexec.c_str(), readresponse);
                }
                else if (words[0] == "backup")
                {
                    string toexec;

                    if (!strstr (commandtoexec,"path-display-size"))
                    {
                        unsigned int width = getNumberOfCols(75);
                        int pathSize = int((width-21)/2);

                        toexec+="backup --path-display-size=";
                        toexec+=SSTR(pathSize);
                        toexec+=" ";
                        if (strlen(commandtoexec)>strlen("backup "))
                        {
                            toexec+=commandtoexec+strlen("backup ");
                        }
                    }
                    else
                    {
                        toexec+=commandtoexec;
                    }

                    comms->executeCommand(toexec.c_str(), readresponse);
                }
                else
                {
                    if ( words[0] == "get" || words[0] == "put" || words[0] == "reload")
                    {
                        string s = commandtoexec;
                        if (clientID.size())
                        {
                            string sline = commandtoexec;
                            size_t pspace = sline.find_first_of(" ");
                            s="";
                            s=sline.substr(0,pspace);
                            s += " --clientID=";
                            s+=clientID;
                            if (pspace!=string::npos)
                            {
                                s+=sline.substr(pspace);
                            }
                            words.push_back(s);
                        }
                        comms->executeCommand(s, readresponse);
#ifdef _WIN32
                        Sleep(200); // give a brief while to print progress ended
#endif
                    }
                    else
                    {
                        // execute user command
                        comms->executeCommand(commandtoexec, readresponse);
                    }
                }
            }
            else
            {
                cerr << "failed to interprete input commandtoexec: " << commandtoexec << endl;
            }
            break;
        }
    }

}

// main loop
#ifndef NO_READLINE
void readloop()
{
    time_t lasttimeretrycons = 0;

    char *saved_line = NULL;
    int saved_point = 0;

    rl_save_prompt();

    int readline_fd = -1;

    readline_fd = fileno(rl_instream);

    procesingline = true;

    comms->registerForStateChanges(true, statechangehandle);

    if (!comms->waitForServerReadyOrRegistrationFailed(std::chrono::seconds(2*RESUME_SESSION_TIMEOUT)))
    {
        std::cerr << "Server seems irresponsive" << endl;
    }

    procesingline = false;
    promptreinstalledwhenprocessingline = false;


    for (;; )
    {
        if (prompt == COMMAND)
        {
            mutexPrompt.lock();
            if (requirepromptinstall)
            {
                install_rl_handler(*dynamicprompt ? dynamicprompt : prompts[COMMAND], false);

                // display prompt
                if (saved_line)
                {
                    rl_replace_line(saved_line, 0);
                    free(saved_line);
                    saved_line = NULL;
                }

                rl_point = saved_point;
                rl_redisplay();
            }
            mutexPrompt.unlock();
        }



        // command editing loop - exits when a line is submitted
        for (;; )
        {
            if (prompt == COMMAND || prompt == AREYOUSURE)
            {
                procesingline = false;
                promptreinstalledwhenprocessingline = false;

                wait_for_input(readline_fd);

                bool retryComms = false;
                time_t tnow = time(NULL);
                {
                    std::lock_guard<std::mutex> g(mutexPrompt);

                    rl_callback_read_char(); //this calls store_line if last char was enter

                    if ( (tnow - lasttimeretrycons) > 5 && !doExit && !comms->isServerUpdating())
                    {
                        char * sl = rl_copy_text(0, rl_end);
                        if (string("quit").find(sl) != 0 && string("exit").find(sl) != 0)
                        {
                            retryComms = true;
                        }
                        free(sl);
                    }

                    rl_resize_terminal(); // to always adjust to new screen sizes

                    if (doExit)
                    {
                        if (saved_line != NULL)
                            free(saved_line);
                        saved_line = NULL;
                        return;
                    }
                }

                if (retryComms)
                {
                    comms->executeCommand("retrycons");
                    lasttimeretrycons = tnow;
                }

            }
            else
            {
                console_readpwchar(pw_buf, sizeof pw_buf, &pw_buf_pos, &g_line);
            }

            if (g_line)
            {
                break;
            }

        }

        cleanLastMessage();// clean last message that avoids broadcasts repetitions

        mutexPrompt.lock();
        // save line
        saved_point = rl_point;
        if (saved_line != NULL)
            free(saved_line);
        saved_line = rl_copy_text(0, rl_end);

        // remove prompt
        rl_save_prompt();
        rl_replace_line("", 0);
        rl_redisplay();

        mutexPrompt.unlock();
        if (g_line)
        {
            if (strlen(g_line))
            {
                alreadyFinished = false;
                percentDowloaded = 0.0;

                handlerOverridenByExternalThread = false;
                process_line(g_line);

                {
                    //after processing the line, we want to reinstall the handler (except if during the process, or due to it,
                    // the handler is reinstalled by e.g: a change in prompt)
                    std::lock_guard<std::mutex> lkrlhandler(handlerInstallerMutex);
                    if (!handlerOverridenByExternalThread)
                    {
                        requirepromptinstall = true;
                    }
                }

                if (comms->registerRequired())
                {
                     comms->registerForStateChanges(true, statechangehandle);
                }

                // sleep, so that in case there was a changeprompt waiting, gets executed before relooping
                // this is not 100% guaranteed to happen
                sleepSeconds(0);
            }
            free(g_line);
            g_line = NULL;
        }
        if (doExit)
        {
            if (saved_line != NULL)
                free(saved_line);
            saved_line = NULL;
            return;
        }
    }
}
#else  // NO_READLINE
void readloop()
{
    time_t lasttimeretrycons = 0;

    comms->registerForStateChanges(true, statechangehandle);

    //give it a while to communicate the state
    sleepMilliSeconds(700);

    for (;; )
    {
        if (prompt == COMMAND)
        {
            console->updateInputPrompt(*dynamicprompt ? dynamicprompt : prompts[COMMAND]);
        }

        // command editing loop - exits when a line is submitted
        for (;; )
        {
            g_line = console->checkForCompletedInputLine();


            if (g_line)
            {
                break;
            }
            else
            {
                time_t tnow = time(NULL);
                if ((tnow - lasttimeretrycons) > 5 && !doExit && !comms->isServerUpdating())
                {
                    if (wstring(L"quit").find(console->getInputLineToCursor()) != 0 &&
                         wstring(L"exit").find(console->getInputLineToCursor()) != 0   )
                    {
                        comms->executeCommand("retrycons");
                        lasttimeretrycons = tnow;
                    }
                }

                if (doExit)
                {
                    return;
                }
            }
        }

        cleanLastMessage();// clean last message that avoids broadcasts repetitions

        if (g_line)
        {
            if (strlen(g_line))
            {
                alreadyFinished = false;
                percentDowloaded = 0.0;
//                mutexPrompt.lock();
                process_line(g_line);
                requirepromptinstall = true;
//                mutexPrompt.unlock();

                if (comms->registerRequired())
                {
                    comms->registerForStateChanges(true, statechangehandle);
                }

                // sleep, so that in case there was a changeprompt waiting, gets executed before relooping
                // this is not 100% guaranteed to happen
                sleepSeconds(0);
            }
            free(g_line);
            g_line = NULL;
        }
        if (doExit)
        {
            return;
        }
    }
}
#endif

class NullBuffer : public std::streambuf
{
public:
    int overflow(int c)
    {
        return c;
    }
};

void printWelcomeMsg(unsigned int width)
{
    if (!width)
    {
        width = getNumberOfCols(75);
    }

    std::ostringstream oss;

    oss << endl;
    oss << ".";
    for (unsigned int i = 0; i < width; i++)
        oss << "=" ;
    oss << ".";
    oss << endl;
    printCenteredLine(oss, " __  __ _____ ____    _                      _ ",width);
    printCenteredLine(oss, "|  \\/  | ___|/ ___|  / \\   ___ _ __ ___   __| |",width);
    printCenteredLine(oss, "| |\\/| | \\  / |  _  / _ \\ / __| '_ ` _ \\ / _` |",width);
    printCenteredLine(oss, "| |  | | /__\\ |_| |/ ___ \\ (__| | | | | | (_| |",width);
    printCenteredLine(oss, "|_|  |_|____|\\____/_/   \\_\\___|_| |_| |_|\\__,_|",width);

    oss << "|";
    for (unsigned int i = 0; i < width; i++)
        oss << " " ;
    oss << "|";
    oss << endl;
    printCenteredLine(oss, "Welcome to MEGAcmd! A Command Line Interactive and Scriptable",width);
    printCenteredLine(oss, "Application to interact with your MEGA account.",width);
    printCenteredLine(oss, "Please write to support@mega.nz if you find any issue or",width);
    printCenteredLine(oss, "have any suggestion concerning its functionalities.",width);
    printCenteredLine(oss, "Enter \"help --non-interactive\" to learn how to use MEGAcmd with scripts.",width);
    printCenteredLine(oss, "Enter \"help\" for basic info and a list of available commands.",width);

#if defined(_WIN32) && defined(NO_READLINE)
    printCenteredLine(oss, "Unicode support in the console is improved, see \"help --unicode\"", width);
#elif defined(_WIN32)
    printCenteredLine(oss, "Enter \"help --unicode\" for info regarding non-ASCII support.",width);
#endif

    oss << "`";
    for (unsigned int i = 0; i < width; i++)
    {
        oss << "=" ;
    }

#ifndef _WIN32
    oss << "\u00b4\n";
    COUT << oss.str() << std::flush;
#else
    WindowsUtf8StdoutGuard utf8Guard;
    // So far, all is ASCII.
    COUT << oss.str();

    // Now let's tray the non ascii forward acute. Note: codepage should have been set to UTF-8
    // set via console->setShellConsole(CP_UTF8, GetConsoleOutputCP());
    assert(GetConsoleOutputCP() == CP_UTF8);

    if (!(COUT << L"\u00b4")) // still, Windows 7 or depending on the fonts, the console may struggle to render this
    {
        COUT << "/"; //fallback character
    }

    COUT << endl;
#endif
}

int quote_detector(char *line, int index)
{
    return (
        index > 0 &&
        line[index - 1] == '\\' &&
        !quote_detector(line, index - 1)
    );
}

bool runningInBackground()
{
#ifndef _WIN32
    pid_t fg = tcgetpgrp(STDIN_FILENO);
    if(fg == -1) {
        // Piped:
        return false;
    }  else if (fg == getpgrp()) {
        // foreground
        return false;
    } else {
        // background
        return true;
    }
#endif
    return false;
}

#ifndef NO_READLINE
std::string readresponse(const char* question)
{
    string response;
    auto responseRaw = readline(question);
    if (responseRaw)
    {
        response = responseRaw;
    }
    rl_set_prompt("");
    rl_replace_line("", 0);

    rl_callback_handler_remove(); //To fix broken readline (e.g: upper key wouldnt work)

    return response;
}
#else
std::string readresponse(const char* question)
{
    std::string questionStr(question);
    size_t pos = questionStr.rfind('\n');

    if (pos != std::string::npos)
    {
        std::string questionPrev  = questionStr.substr(0, pos);
        std::string prompt = questionStr.substr(pos + 1);

        COUT << questionPrev << std::endl;
        console->updateInputPrompt(prompt);
    }
    else
    {
        console->updateInputPrompt(questionStr);
    }

    for (;;)
    {
        if (char* line = console->checkForCompletedInputLine())
        {
            console->updateInputPrompt("");
            string response(line);
            free(line);
            return response;
        }
        else
        {
            sleepMilliSeconds(200);
        }
    }
}
#endif

} //end namespace

using namespace megacmd;

int main(int argc, char* argv[])
{

    // intialize the comms object
#if defined(_WIN32)
    comms = new MegaCmdShellCommunicationsNamedPipes();
#else
    comms = new MegaCmdShellCommunicationsPosix();
#endif

#ifndef NO_READLINE
    rl_attempted_completion_function = getCompletionMatches;
    rl_completer_quote_characters = "\"'";
    rl_filename_quote_characters  = " ";
    rl_completer_word_break_characters = (char *)" ";

    rl_char_is_quoted_p = &quote_detector;

    if (!runningInBackground())
    {
        rl_initialize(); // initializes readline,
        // so that we can use rl_message or rl_resize_terminal safely before ever
        // prompting anything.
    }
#endif

#if defined(_WIN32) && defined(NO_READLINE)
    console = new CONSOLE_CLASS;
    console->setAutocompleteSyntax(buildAutocompleteSyntax());
    console->setAutocompleteFunction(remote_completion);
    console->setShellConsole(CP_UTF8, GetConsoleOutputCP());
    console->blockingConsolePeek = true;
#endif

#ifdef _WIN32
    // in windows, rl_resize_terminal fails to resize before first prompt appears, we take the width from elsewhere
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int columns;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    columns = csbi.srWindow.Right - csbi.srWindow.Left - 2;
    printWelcomeMsg(columns);
#else
    sleepMilliSeconds(200); // this gives a little while so that the console is ready and rl_resize_terminal works fine
    printWelcomeMsg();
#endif

    readloop();

#ifndef NO_READLINE
    clear_history();
    if (!doReboot)
    {
        rl_callback_handler_remove(); //To avoid having the terminal messed up (requiring a "reset")
    }
#endif
    comms->shutdown();
    delete comms;

    if (doReboot)
    {
#ifdef _WIN32
        sleepSeconds(5); // Give a while for server to restart
        LPWSTR szPathExecQuoted = GetCommandLineW();
        wstring wspathexec = wstring(szPathExecQuoted);

        if (wspathexec.at(0) == '"')
        {
            wspathexec = wspathexec.substr(1);
        }
        while (wspathexec.size() && ( wspathexec.at(wspathexec.size()-1) == '"' || wspathexec.at(wspathexec.size()-1) == ' ' ))
        {
            wspathexec = wspathexec.substr(0,wspathexec.size()-1);
        }
        LPWSTR szPathExec = (LPWSTR) wspathexec.c_str();

        STARTUPINFO si;
        PROCESS_INFORMATION pi;
        ZeroMemory( &si, sizeof(si) );
        ZeroMemory( &pi, sizeof(pi) );
        si.cb = sizeof(si);

        if (!CreateProcess( szPathExec,szPathExec,NULL,NULL,TRUE,
                            CREATE_NEW_CONSOLE,
                            NULL,NULL,
                            &si,&pi) )
        {
            COUT << "Unable to execute: " << szPathExec << " errno = : " << ERRNO << endl;
            sleepSeconds(5);
        }
#elif defined(__linux__)
        system("reset -I");
        string executable = argv[0];
        if (executable.find("/") != 0)
        {
            executable.insert(0, getCurrentExecPath()+"/");
        }
        execv(executable.c_str(), argv);
#else
        system("reset -I");
        execv(argv[0], argv);
#endif

    }
}
