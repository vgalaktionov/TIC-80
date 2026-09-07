// Bounded boot log and single-task LAN HTTP endpoint for bare-metal diagnostics.

#include "debuglog.h"
#include "logring.h"

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
static const unsigned IoTimeoutMs = 5000;
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
    return static_cast<unsigned>(CTimer::GetClockTicks() - start) >= IoTimeoutMs * 1000;
}

static boolean sendAll(CSocket* socket, const void* data, unsigned length)
{
    const char* bytes = static_cast<const char*>(data);
    unsigned sent = 0;
    const unsigned start = CTimer::GetClockTicks();

    while (sent < length && !deadlineExpired(start))
    {
        int result = socket->Send(bytes + sent, length - sent, MSG_DONTWAIT);
        if (result < 0) return FALSE;
        if (result == 0)
        {
            CScheduler::Get()->MsSleep(PollMs);
            continue;
        }

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
