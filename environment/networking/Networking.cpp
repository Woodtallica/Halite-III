#include "Networking.hpp"

#include <sstream>
#include <thread>

std::mutex coutMutex;

std::string serializeMapSize(const hlt::Map& map) {
    std::string returnString = "";
    std::ostringstream oss;
    oss << map.map_width << ' ' << map.map_height << ' ';
    returnString = oss.str();
    return returnString;
}

std::string Networking::serializeMap(const hlt::Map& map) {
    std::string returnString = "";
    std::ostringstream oss;

    // Encode individual ships
    oss << "ships";
    for (hlt::PlayerId playerId = 0; playerId < numberOfPlayers();
         playerId++) {
        oss << " player " << (int) playerId;

        for (hlt::EntityIndex ship_id = 0; ship_id < hlt::MAX_PLAYER_SHIPS;
             ship_id++) {
            const auto& ship = map.ships[playerId][ship_id];
            if (!ship.is_alive()) continue;

            oss << ' ' << ship_id;
            oss << ' ' << ship.location.pos_x;
            oss << ' ' << ship.location.pos_y;
            oss << ' ' << ship.health;
            oss << ' ' << ship.orientation;
            // TODO: send other ship information
        }
    }

    oss << " planets";
    for (hlt::EntityIndex planet_id = 0; planet_id < map.planets.size();
         planet_id++) {
        const auto& planet = map.planets[planet_id];
        if (!planet.is_alive()) continue;

        oss << ' ' << planet_id;
        oss << ' ' << planet.location.pos_x;
        oss << ' ' << planet.location.pos_y;
        oss << ' ' << planet.health;
    }

    returnString = oss.str();

    return returnString;
}

auto is_valid_move_character(const char& c) -> bool {
    return (c >= '0' && c <= '9')
        || c == ' '
        || c == '-'
        || c == 'r'
        || c == 't'
        || c == 'u'
        || c == 'd';
}

auto eject_bot(std::string message) -> std::string {
    if (!quiet_output) {
        std::lock_guard<std::mutex> guard(coutMutex);
        std::cout << message;
    }
    return message;
}

void Networking::deserializeMoveSet(std::string& inputString,
                                    const hlt::Map& m,
                                    hlt::PlayerMoveQueue& moves) {
    if (std::find_if(inputString.begin(),
                     inputString.end(),
                     [](const char& c) -> bool {
                         return !is_valid_move_character(c);
                     }) != inputString.end()) {
        throw eject_bot(
            "Bot sent an invalid character - ejecting from game.\n");
    }

    std::stringstream iss(inputString);

    hlt::Move move;
    // Keep track of how many queued commands each ship has
    std::array<int, hlt::MAX_PLAYER_SHIPS> queue_depth = { { 0 } };

    char command;
    while (iss >> command) {
        switch (command) {
            case 'r': {
                move.type = hlt::MoveType::Rotate;
                iss >> move.shipId;
                iss >> move.move.rotate_by;
                const auto thrust = move.move.rotate_by;
                if (thrust < -100 || thrust > 100) {
                    std::string errorMessage =
                        "Bot sent an invalid rotation thrust - ejecting from game.\n";
                    if (!quiet_output) {
                        std::lock_guard<std::mutex> guard(coutMutex);
                        std::cout << errorMessage;
                    }
                    throw errorMessage;
                }
                break;
            }
            case 't': {
                move.type = hlt::MoveType::Thrust;
                iss >> move.shipId;
                iss >> move.move.thrust_by;
                const auto thrust = move.move.thrust_by;
                if (thrust < -100 || thrust > 100) {
                    std::string errorMessage =
                        "Bot sent an invalid rotation thrust - ejecting from game.\n";
                    if (!quiet_output) {
                        std::lock_guard<std::mutex> guard(coutMutex);
                        std::cout << errorMessage;
                    }
                    throw errorMessage;
                }
                break;
            }
            case 'd': {
                // TODO: don't validate this here since planet can die
                // between commands anyways
                move.type = hlt::MoveType::Dock;
                iss >> move.shipId;
                iss >> move.move.dock_to;
                if (move.move.dock_to >= m.planets.size()
                    || !m.planets[move.move.dock_to].is_alive()) {
                    throw eject_bot(
                        "Bot docked to invalid planet - ejecting from game.\n");
                }
                break;
            }
            case 'u': {
                move.type = hlt::MoveType::Undock;
                iss >> move.shipId;
                break;
            }
            default:
                throw eject_bot(
                    "Bot sent an invalid command - ejecting from game.\n");
        }

        auto queue_index = queue_depth.at(move.shipId);
        if (queue_index < hlt::MAX_QUEUED_MOVES) {
            moves.at(queue_index).at(move.shipId) = move;
            queue_depth.at(move.shipId)++;
        } else {
            // TODO: refactor this into its own helper
            std::string errorMessage =
                "Bot tried to queue too many moves for a particular ship - ejecting from game.\n";
            if (!quiet_output) {
                std::lock_guard<std::mutex> guard(coutMutex);
                std::cout << errorMessage;
            }
            throw errorMessage;
        }
        move = {};
    }
}

void Networking::sendString(unsigned char playerTag,
                            std::string& sendString) {
    //End message with newline character
    sendString += '\n';

#ifdef _WIN32
    WinConnection connection = connections[playerTag - 1];

    DWORD charsWritten;
    bool success;
    success = WriteFile(connection.write, sendString.c_str(), sendString.length(), &charsWritten, NULL);
    if(!success || charsWritten == 0) {
        if(!quiet_output) std::cout << "Problem writing to pipe\n";
        throw 1;
    }
#else
    UniConnection connection = connections[playerTag - 1];
    ssize_t charsWritten =
        write(connection.write, sendString.c_str(), sendString.length());
    if (charsWritten < sendString.length()) {
        if (!quiet_output) std::cout << "Problem writing to pipe\n";
        throw 1;
    }
#endif
}

std::string Networking::getString(unsigned char playerTag,
                                  const unsigned int timeoutMillis) {

    std::string newString;
    int timeoutMillisRemaining = timeoutMillis;
    std::chrono::high_resolution_clock::time_point
        tp = std::chrono::high_resolution_clock::now();

#ifdef _WIN32
    WinConnection connection = connections[playerTag - 1];

    DWORD charsRead;
    bool success;
    char buffer;

    //Keep reading char by char until a newline
    while(true) {
        timeoutMillisRemaining = timeoutMillis - std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - tp).count();
        if(timeoutMillisRemaining < 0) throw newString;
        //Check to see that there are bytes in the pipe before reading
        //Throw error if no bytes in alloted time
        //Check for bytes before sampling clock, because reduces latency (vast majority the pipe is alread full)
        DWORD bytesAvailable = 0;
        PeekNamedPipe(connection.read, NULL, 0, NULL, &bytesAvailable, NULL);
        if(bytesAvailable < 1) {
            std::chrono::high_resolution_clock::time_point initialTime = std::chrono::high_resolution_clock::now();
            while (bytesAvailable < 1) {
                if(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - initialTime).count() > timeoutMillisRemaining) throw newString;
                PeekNamedPipe(connection.read, NULL, 0, NULL, &bytesAvailable, NULL);
            }
        }

        success = ReadFile(connection.read, &buffer, 1, &charsRead, NULL);
        if(!success || charsRead < 1) {
            if(!quiet_output) {
                std::string errorMessage = "Bot #" + std::to_string(playerTag) + " timed out or errored (Windows)\n";
                std::lock_guard<std::mutex> guard(coutMutex);
                std::cout << errorMessage;
            }
            throw newString;
        }
        if(buffer == '\n') break;
        else newString += buffer;
    }
#else
    UniConnection connection = connections[playerTag - 1];

    fd_set set;
    FD_ZERO(&set); /* clear the set */
    // TODO: remove this
    if (connection.read > FD_SETSIZE) assert(false);
    FD_SET(connection.read, &set); /* add our file descriptor to the set */
    char buffer;

    //Keep reading char by char until a newline
    while (true) {

        //Check if there are bytes in the pipe
        timeoutMillisRemaining = timeoutMillis
            - std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - tp).count();
        if (timeoutMillisRemaining < 0) throw newString;
        struct timeval timeout;
        timeout.tv_sec = timeoutMillisRemaining / 1000.0;
        timeout.tv_usec = (timeoutMillisRemaining % 1000) * 1000;
        int selectionResult =
            select(connection.read + 1, &set, NULL, NULL, &timeout);

        if (selectionResult > 0) {
            read(connection.read, &buffer, 1);

            if (buffer == '\n') break;
            else newString += buffer;
        } else {
            if (!quiet_output) {
                std::string errorMessage = "Bot #" + std::to_string(playerTag)
                    + " timeout or error (Unix) "
                    + std::to_string(selectionResult) + '\n';
                std::lock_guard<std::mutex> guard(coutMutex);
                std::cout << errorMessage;
            }
            throw newString;
        }
    }
#endif
    //Python turns \n into \r\n
    if (newString.back() == '\r') newString.pop_back();

    return newString;
}

void Networking::startAndConnectBot(std::string command) {
#ifdef _WIN32

    command = "/C " + command;

    WinConnection parentConnection, childConnection;

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    //Child stdout pipe
    if(!CreatePipe(&parentConnection.read, &childConnection.write, &saAttr, 0)) {
        if(!quiet_output) std::cout << "Could not create pipe\n";
        throw 1;
    }
    if(!SetHandleInformation(parentConnection.read, HANDLE_FLAG_INHERIT, 0)) throw 1;

    //Child stdin pipe
    if(!CreatePipe(&childConnection.read, &parentConnection.write, &saAttr, 0)) {
        if(!quiet_output) std::cout << "Could not create pipe\n";
        throw 1;
    }
    if(!SetHandleInformation(parentConnection.write, HANDLE_FLAG_INHERIT, 0)) throw 1;

    //MAKE SURE THIS MEMORY IS ERASED
    PROCESS_INFORMATION piProcInfo;
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

    STARTUPINFO siStartInfo;
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.hStdError = childConnection.write;
    siStartInfo.hStdOutput = childConnection.write;
    siStartInfo.hStdInput = childConnection.read;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    //C:/xampp/htdocs/Halite/Halite/Debug/ExampleBot.exe
    //C:/Users/Michael/Anaconda3/python.exe
    //C:/Program Files/Java/jre7/bin/java.exe -cp C:/xampp/htdocs/Halite/AIResources/Java MyBot
    bool success = CreateProcess(
        "C:\\windows\\system32\\cmd.exe",
        LPSTR(command.c_str()),     //command line
        NULL,          //process security attributes
        NULL,          //primary thread security attributes
        TRUE,          //handles are inherited
        0,             //creation flags
        NULL,          //use parent's environment
        NULL,          //use parent's current directory
        &siStartInfo,  //STARTUPINFO pointer
        &piProcInfo
    );  //receives PROCESS_INFORMATION
    if(!success) {
        if(!quiet_output) std::cout << "Could not start process\n";
        throw 1;
    }
    else {
        CloseHandle(piProcInfo.hProcess);
        CloseHandle(piProcInfo.hThread);

        processes.push_back(piProcInfo.hProcess);
        connections.push_back(parentConnection);
    }

#else

    if (!quiet_output) std::cout << command << "\n";

    pid_t pid;
    int writePipe[2];
    int readPipe[2];

    if (pipe(writePipe)) {
        if (!quiet_output) std::cout << "Error creating pipe\n";
        throw 1;
    }
    if (pipe(readPipe)) {
        if (!quiet_output) std::cout << "Error creating pipe\n";
        throw 1;
    }

    pid_t ppid_before_fork = getpid();

    //Fork a child process
    pid = fork();
    if (pid == 0) { //This is the child
        setpgid(getpid(), getpid());

#ifdef __linux__
        // install a parent death signal
        // http://stackoverflow.com/a/36945270
        int r = prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (r == -1)
        {
            if(!quiet_output) std::cout << "Error installing parent death signal\n";
            throw 1;
        }
        if (getppid() != ppid_before_fork)
            exit(1);
#endif

        dup2(writePipe[0], STDIN_FILENO);

        dup2(readPipe[1], STDOUT_FILENO);
        dup2(readPipe[1], STDERR_FILENO);

        execl("/bin/sh", "sh", "-c", command.c_str(), (char*) NULL);

        //Nothing past the execl should be run

        exit(1);
    } else if (pid < 0) {
        if (!quiet_output) std::cout << "Fork failed\n";
        throw 1;
    }

    UniConnection connection;
    connection.read = readPipe[0];
    connection.write = writePipe[1];

    connections.push_back(connection);
    processes.push_back(pid);

#endif

    player_logs.push_back(std::string());
}

int Networking::handleInitNetworking(unsigned char playerTag,
                                     const hlt::Map& m,
                                     bool ignoreTimeout,
                                     std::string* playerName) {

    const int ALLOTTED_MILLIS = ignoreTimeout ? 2147483647 : 30000;

    std::string response;
    try {
        std::string playerTagString = std::to_string(playerTag),
            mapSizeString = serializeMapSize(m), mapString = serializeMap(m);
        sendString(playerTag, playerTagString);
        sendString(playerTag, mapSizeString);
        sendString(playerTag, mapString);
        std::string outMessage =
            "Init Message sent to player " + std::to_string(int(playerTag))
                + ".\n";
        if (!quiet_output) std::cout << outMessage;

        player_logs[playerTag - 1] += " --- Init ---\n";

        std::chrono::high_resolution_clock::time_point
            initialTime = std::chrono::high_resolution_clock::now();
        response = getString(playerTag, ALLOTTED_MILLIS);
        unsigned int millisTaken =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now()
                    - initialTime).count();

        player_logs[playerTag - 1] +=
            response + "\n --- Bot used " + std::to_string(millisTaken)
                + " milliseconds ---";

        *playerName = response.substr(0, 30);
        if (!quiet_output) {
            std::string inMessage = "Init Message received from player "
                + std::to_string(int(playerTag)) + ", " + *playerName + ".\n";
            std::cout << inMessage;
        }

        return millisTaken;
    }
    catch (std::string s) {
        if (s.empty())
            player_logs[playerTag - 1] += "\nERRORED!\nNo response received.";
        else
            player_logs[playerTag - 1] +=
                "\nERRORED!\nResponse received (if any):\n" + s;
        *playerName =
            "Bot #" + std::to_string(playerTag) + "; timed out during Init";
    }
    catch (...) {
        if (response.empty())
            player_logs[playerTag - 1] += "\nERRORED!\nNo response received.";
        else
            player_logs[playerTag - 1] +=
                "\nERRORED!\nResponse received (if any):\n" + response;
        *playerName =
            "Bot #" + std::to_string(playerTag) + "; timed out during Init";
    }
    return -1;
}

int Networking::handleFrameNetworking(unsigned char playerTag,
                                      const unsigned short& turnNumber,
                                      const hlt::Map& m,
                                      bool ignoreTimeout,
                                      hlt::PlayerMoveQueue& moves) {

    const int ALLOTTED_MILLIS = ignoreTimeout ? 2147483647 : 1500;

    std::string response;
    try {
        if (isProcessDead(playerTag)) return -1;

        //Send this bot the game map and the messages addressed to this bot
        std::string mapString = serializeMap(m);
        sendString(playerTag, mapString);

        // TODO: clear out array

        player_logs[playerTag - 1] +=
            "\n-----------------------------------------------------------------------------\n --- Frame #"
                + std::to_string(turnNumber) + " ---\n";

        std::chrono::high_resolution_clock::time_point
            initialTime = std::chrono::high_resolution_clock::now();
        response = getString(playerTag, ALLOTTED_MILLIS);
        unsigned int millisTaken =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now()
                    - initialTime).count();

        player_logs[playerTag - 1] +=
            response + "\n --- Bot used " + std::to_string(millisTaken)
                + " milliseconds ---";

        deserializeMoveSet(response, m, moves);

        return millisTaken;
    }
    catch (std::string s) {
        if (s.empty())
            player_logs[playerTag - 1] += "\nERRORED!\nNo response received.";
        else
            player_logs[playerTag - 1] +=
                "\nERRORED!\nResponse received (if any):\n" + s;
    }
    catch (...) {
        if (response.empty())
            player_logs[playerTag - 1] += "\nERRORED!\nNo response received.";
        else
            player_logs[playerTag - 1] +=
                "\nERRORED!\nResponse received (if any):\n" + response;
    }
    return -1;
}

void Networking::killPlayer(unsigned char playerTag) {
    if (isProcessDead(playerTag)) return;

    std::string newString;
    const int PER_CHAR_WAIT = 10; //millis
    const int MAX_READ_TIME = 1000; //millis

#ifdef _WIN32

    //Try to read entire contents of pipe.
    WinConnection connection = connections[playerTag - 1];
    DWORD charsRead;
    bool success;
    char buffer;
    std::chrono::high_resolution_clock::time_point tp = std::chrono::high_resolution_clock::now();
    while(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - tp).count() < MAX_READ_TIME) {
        DWORD bytesAvailable = 0;
        PeekNamedPipe(connection.read, NULL, 0, NULL, &bytesAvailable, NULL);
        if(bytesAvailable < 1) {
            std::chrono::high_resolution_clock::time_point initialTime = std::chrono::high_resolution_clock::now();
            while(bytesAvailable < 1) {
                if(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - initialTime).count() > PER_CHAR_WAIT) break;
                PeekNamedPipe(connection.read, NULL, 0, NULL, &bytesAvailable, NULL);
            }
            if(bytesAvailable < 1) break; //Took too long to get a character; breaking.
        }

        success = ReadFile(connection.read, &buffer, 1, &charsRead, NULL);
        if(!success || charsRead < 1) {
            if(!quiet_output) {
                std::string errorMessage = "Bot #" + std::to_string(playerTag) + " timed out or errored (Windows)\n";
                std::lock_guard<std::mutex> guard(coutMutex);
                std::cout << errorMessage;
            }
            break;
        }
        newString += buffer;
    }

    HANDLE process = processes[playerTag - 1];

    TerminateProcess(process, 0);

    processes[playerTag - 1] = NULL;
    connections[playerTag - 1].read = NULL;
    connections[playerTag - 1].write = NULL;

    std::string deadMessage = "Player " + std::to_string(playerTag) + " is dead\n";
    if(!quiet_output) std::cout << deadMessage;

#else

    //Try to read entire contents of pipe.
    UniConnection connection = connections[playerTag - 1];
    fd_set set;
    FD_ZERO(&set); /* clear the set */
    FD_SET(connection.read, &set); /* add our file descriptor to the set */
    char buffer;
    std::chrono::high_resolution_clock::time_point
        tp = std::chrono::high_resolution_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - tp).count()
        < MAX_READ_TIME) {
        struct timeval timeout;
        timeout.tv_sec = PER_CHAR_WAIT / 1000;
        timeout.tv_usec = (PER_CHAR_WAIT % 1000) * 1000;
        int selectionResult =
            select(connection.read + 1, &set, NULL, NULL, &timeout);
        if (selectionResult > 0) {
            read(connection.read, &buffer, 1);
            newString += buffer;
        } else break;
    }

    kill(-processes[playerTag - 1], SIGKILL);

    processes[playerTag - 1] = -1;
    connections[playerTag - 1].read = -1;
    connections[playerTag - 1].write = -1;
#endif

    if (!newString.empty())
        player_logs[playerTag - 1] +=
            "\n --- Bot was killed. Below is the rest of its output (if any): ---\n"
                + newString + "\n --- End bot output ---";
}

bool Networking::isProcessDead(unsigned char playerTag) {
#ifdef _WIN32
    return processes[playerTag - 1] == NULL;
#else
    return processes.at(playerTag - 1) == -1;
#endif
}

int Networking::numberOfPlayers() {
#ifdef _WIN32
    return connections.size();
#else
    return connections.size();
#endif
}
