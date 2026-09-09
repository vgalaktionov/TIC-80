// Bounded boot log and single-task LAN HTTP endpoint for bare-metal diagnostics.

#include "debuglog.h"
#include "logring.h"
#include "hdmi_recovery.h"

#include <circle/net/in.h>
#include <circle/net/ipaddress.h>
#include <circle/net/netsubsystem.h>
#include <circle/net/socket.h>
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/spinlock.h>
#include <circle/timer.h>
#include <cstdio>
#include <cstring>

namespace
{
static const unsigned LogCapacity = 256 * 1024;
static const unsigned RequestCapacity = 2048;
static const unsigned RequestTimeoutMs = 5000;
static const unsigned ResponseChunkSize = 4096;
static const unsigned PollMs = 10;

static Tic80LogRing<LogCapacity> Log;
static CSpinLock LogLock(IRQ_LEVEL);
static boolean ServerStarted;

static boolean containsHeaderEnd(const char* request, unsigned length)
{
    return length >= 4 && strstr(request, "\r\n\r\n") != 0;
}

static boolean deadlineExpired(unsigned start)
{
    return static_cast<unsigned>(CTimer::GetClockTicks() - start) >= RequestTimeoutMs * 1000;
}

static boolean sendAll(CSocket* socket, const void* data, unsigned length)
{
    const char* bytes = static_cast<const char*>(data);
    unsigned sent = 0;
    while (sent < length)
    {
        const unsigned remaining = length - sent;
        const unsigned chunk = remaining < ResponseChunkSize ? remaining : ResponseChunkSize;
        const int result = socket->Send(bytes + sent, chunk, 0);
        if (result != static_cast<int>(chunk)) return FALSE;

        sent += static_cast<unsigned>(result);
    }

    return sent == length;
}

class CDebugLogServer : public CTask
{
public:
    CDebugLogServer(CNetSubSystem* network, unsigned port)
        : mNetwork(network), mPort(static_cast<u16>(port))
    {
        SetName("tic80-debug-log");
    }

    void Run() override
    {
        while (!mNetwork->IsRunning()) CScheduler::Get()->MsSleep(100);

        CSocket listener(mNetwork, IPPROTO_TCP);
        if (listener.Bind(mPort) < 0 || listener.Listen(2) < 0)
        {
            char message[80];
            snprintf(message, sizeof message,
                     "[tic80] debug log: cannot listen on TCP port %u\n", mPort);
            tic80DebugLogWrite(message);
            return;
        }

        char message[80];
        snprintf(message, sizeof message,
                 "[tic80] debug log: listening on TCP port %u\n", mPort);
        tic80DebugLogWrite(message);
        while (TRUE)
        {
            CIPAddress address;
            u16 port;
            CSocket* connection = listener.Accept(&address, &port);
            if (!connection)
            {
                CScheduler::Get()->MsSleep(PollMs);
                continue;
            }
            serve(connection);
            delete connection;
        }
    }

private:
    void serve(CSocket* socket)
    {
        char request[RequestCapacity + 1];
        unsigned length = 0;
        const unsigned start = CTimer::GetClockTicks();
        boolean complete = FALSE;

        while (length < RequestCapacity && !deadlineExpired(start))
        {
            int result = socket->Receive(request + length, RequestCapacity - length,
                                         MSG_DONTWAIT);
            if (result < 0) return;
            if (result == 0)
            {
                CScheduler::Get()->MsSleep(PollMs);
                continue;
            }

            length += static_cast<unsigned>(result);
            request[length] = '\0';
            if (containsHeaderEnd(request, length))
            {
                complete = TRUE;
                break;
            }
        }

        request[length] = '\0';
        if (!complete)
        {
            static const char BadRequest[] =
                "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            sendAll(socket, BadRequest, sizeof BadRequest - 1);
            return;
        }
        // Queue work for the render task: never call TV service from HTTP.
        // Require a custom header so browser cross-origin forms cannot reset video.
        char* headerEnd = strstr(request, "\r\n\r\n");
        headerEnd[2] = '\0'; // Do not accept a spoofed header in a request body.
        static const char ResetPath[] = "POST /hdmi/reset HTTP/1.1\r\n";
        static const char StatusPath[] = "POST /hdmi/status HTTP/1.1\r\n";
        const bool reset = !strncmp(request, ResetPath, sizeof ResetPath - 1);
        const bool status = !strncmp(request, StatusPath, sizeof StatusPath - 1);
        if (reset || status)
        {
            if (!strstr(request, "\r\nX-TIC80-Debug: 1\r\n"))
            {
                static const char Forbidden[] = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                sendAll(socket, Forbidden, sizeof Forbidden - 1);
                return;
            }
            tic80HdmiRecoveryRequest(reset);
            static const char Accepted[] = "HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            sendAll(socket, Accepted, sizeof Accepted - 1);
            return;
        }
        const boolean validPath = !strncmp(request, "GET / HTTP/", 11)
                                  || !strncmp(request, "GET /log HTTP/", 14);
        if (!validPath)
        {
            static const char NotFound[] =
                "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            sendAll(socket, NotFound, sizeof NotFound - 1);
            return;
        }

        unsigned dropped = 0;
        unsigned writes = 0;
        unsigned bodyLength = tic80DebugLogSnapshot(mSnapshot, sizeof mSnapshot,
                                                     &dropped, &writes);
        char header[384];
        int headerLength = snprintf(
            header, sizeof header,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Cache-Control: no-store\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "X-TIC80-Log-Writes: %u\r\n"
            "X-TIC80-Dropped-Bytes: %u\r\n"
            "Content-Length: %u\r\n"
            "Connection: close\r\n\r\n",
            writes, dropped, bodyLength);
        if (headerLength <= 0 || static_cast<unsigned>(headerLength) >= sizeof header) return;
        if (!sendAll(socket, header, static_cast<unsigned>(headerLength))) return;
        if (bodyLength) sendAll(socket, mSnapshot, bodyLength);
    }

    CNetSubSystem* mNetwork;
    u16 mPort;
    char mSnapshot[LogCapacity];
};
}

void tic80DebugLogWrite(const char* message)
{
    if (!message) return;

    tic80DebugLogWriteBytes(message, strlen(message));
}

void tic80DebugLogWriteBytes(const void* data, unsigned length)
{
    if (!data || length == 0) return;

    LogLock.Acquire();
    Log.Write(data, length);
    LogLock.Release();
}

unsigned tic80DebugLogSnapshot(char* output, unsigned capacity,
                               unsigned* droppedBytes, unsigned* writeCalls)
{
    if (!output || capacity == 0) return 0;

    LogLock.Acquire();
    const unsigned length = Log.Snapshot(output, capacity, droppedBytes, writeCalls);
    LogLock.Release();
    return length;
}

boolean tic80DebugLogServerStart(CNetSubSystem* network, unsigned port)
{
    if (!network || port == 0 || port > 65535 || ServerStarted) return FALSE;
    ServerStarted = TRUE;
    new CDebugLogServer(network, port);
    return TRUE;
}
