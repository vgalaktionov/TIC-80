// MIT License

#include <circle/net/dnsclient.h>
#include <circle/net/httpclient.h>
#include <circle/net/netsubsystem.h>
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/timer.h>
#include <circle/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C"
{
#include "../../studio/net.h"
}

#define HTTP_BUFFER_SIZE (4 * 1024 * 1024)
#define NETWORK_WAIT_MSECS 180000
#define URL_SIZE 2048

CNetSubSystem* getTIC80NetSubsystem();
void tic80SerialDebug(const char* message);

static void logRequest(const char* prefix, const char* url, s32 value)
{
    char message[256];
    snprintf(message, sizeof message, "[tic80] HTTP %s: %.180s (%ld)\n",
             prefix, url, (long)value);
    tic80SerialDebug(message);
}

struct BaremetalHttpRequest
{
    char* url;
    net_get_callback callback;
    void* calldata;
    volatile boolean started;
    volatile boolean complete;
    s32 status;
    u8* data;
    unsigned size;
};

class CHTTPWorkerTask;

struct tic_net
{
    char host[URL_SIZE];
    BaremetalHttpRequest** requests;
    s32 count;
    volatile boolean stopping;
    CHTTPWorkerTask* worker;
};

class CHTTPWorkerTask : public CTask
{
public:
    explicit CHTTPWorkerTask(tic_net* owner)
        : mOwner(owner)
    {
        SetName("tic80-http-worker");
    }

    void Run() override
    {
        while (!mOwner->stopping)
        {
            BaremetalHttpRequest* request = NextRequest();
            if (!request)
            {
                CScheduler::Get()->MsSleep(20);
                continue;
            }

            Process(request);
        }

        for (s32 i = 0; i < mOwner->count; i++)
        {
            BaremetalHttpRequest* request = mOwner->requests[i];
            if (request && !request->complete)
            {
                Finish(request, -5);
            }
        }
    }

private:
    BaremetalHttpRequest* NextRequest()
    {
        for (s32 i = 0; i < mOwner->count; i++)
        {
            BaremetalHttpRequest* request = mOwner->requests[i];
            if (request && !request->started)
            {
                request->started = TRUE;
                return request;
            }
        }

        return NULL;
    }

    void Process(BaremetalHttpRequest* request)
    {
        CNetSubSystem* net = getTIC80NetSubsystem();
        unsigned waited = 0;

        logRequest("GET", request->url, 0);

        while (net && !net->IsRunning() && waited < NETWORK_WAIT_MSECS)
        {
            CScheduler::Get()->MsSleep(100);
            waited += 100;
        }

        if (!net || !net->IsRunning())
        {
            Finish(request, -1);
            return;
        }

        const char* url = request->url;
        static const char HttpPrefix[] = "http://";
        if (strncmp(url, HttpPrefix, sizeof HttpPrefix - 1) != 0)
        {
            Finish(request, -2);
            return;
        }

        const char* hostStart = url + sizeof HttpPrefix - 1;
        const char* path = strchr(hostStart, '/');
        size_t hostLength = path ? (size_t)(path - hostStart) : strlen(hostStart);
        if (hostLength == 0 || hostLength >= 256)
        {
            Finish(request, -2);
            return;
        }

        char host[256];
        memcpy(host, hostStart, hostLength);
        host[hostLength] = '\0';

        u16 port = HTTP_PORT;
        char* portSeparator = strchr(host, ':');
        if (portSeparator)
        {
            *portSeparator++ = '\0';
            unsigned parsedPort = (unsigned)atoi(portSeparator);
            if (parsedPort == 0 || parsedPort > 65535)
            {
                Finish(request, -2);
                return;
            }
            port = (u16)parsedPort;
        }

        CIPAddress serverIP;
        CDNSClient dns(net);
        if (!dns.Resolve(host, &serverIP))
        {
            Finish(request, -3);
            return;
        }

        request->data = (u8*)malloc(HTTP_BUFFER_SIZE);
        if (!request->data)
        {
            Finish(request, -4);
            return;
        }

        unsigned length = HTTP_BUFFER_SIZE;
        CHTTPClient client(net, serverIP, port, host);
        THTTPStatus status = client.Get(path ? path : "/", request->data, &length);
        if (status != HTTPOK)
        {
            free(request->data);
            request->data = NULL;
            Finish(request, (s32)status);
            return;
        }

        request->size = length;
        logRequest("done", request->url, (s32)length);
        Finish(request, 0);
    }

    void Finish(BaremetalHttpRequest* request, s32 status)
    {
        if (status != 0)
        {
            logRequest("error", request->url, status);
        }

        request->status = status;
        request->complete = TRUE;
    }

    tic_net* mOwner;
};

extern "C" tic_net* tic_net_create(const char* host)
{
    tic_net* net = (tic_net*)calloc(1, sizeof *net);
    if (!net)
    {
        return NULL;
    }

    strncpy(net->host, host, sizeof net->host - 1);
    net->worker = new CHTTPWorkerTask(net);
    return net;
}

extern "C" void tic_net_get(tic_net* net, const char* url,
                              net_get_callback callback, void* calldata)
{
    if (!net || !url || !callback)
    {
        return;
    }

    BaremetalHttpRequest* request =
        (BaremetalHttpRequest*)calloc(1, sizeof *request);
    if (!request)
    {
        return;
    }

    size_t length = strlen(url);
    boolean absolute = strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0;
    if (!absolute)
    {
        length += strlen(net->host);
    }

    request->url = (char*)malloc(length + 1);
    if (!request->url)
    {
        free(request);
        return;
    }

    if (absolute)
    {
        strcpy(request->url, url);
    }
    else
    {
        strcpy(request->url, net->host);
        strcat(request->url, url);
    }

    request->callback = callback;
    request->calldata = calldata;

    BaremetalHttpRequest** requests = (BaremetalHttpRequest**)realloc(
        net->requests, sizeof *net->requests * (net->count + 1));
    if (!requests)
    {
        free(request->url);
        free(request);
        return;
    }

    net->requests = requests;
    net->requests[net->count++] = request;
}

extern "C" void tic_net_start(tic_net*)
{
}

extern "C" void tic_net_end(tic_net* net)
{
    if (!net)
    {
        return;
    }

    s32 count = net->count;
    for (s32 i = 0; i < count; i++)
    {
        BaremetalHttpRequest* request = net->requests[i];
        if (!request || !request->complete)
        {
            continue;
        }

        net_get_data result = {};
        result.calldata = request->calldata;
        result.url = request->url;

        if (request->status == 0)
        {
            result.type = net_get_data::net_get_done;
            result.done.data = request->data;
            result.done.size = (s32)request->size;
        }
        else
        {
            result.type = net_get_data::net_get_error;
            result.error.code = request->status;
        }

        request->callback(&result);

        free(request->data);
        free(request->url);
        free(request);
        net->requests[i] = NULL;
    }
}

extern "C" void tic_net_close(tic_net* net)
{
    if (!net)
    {
        return;
    }

    net->stopping = TRUE;
    if (net->worker)
    {
        net->worker->WaitForTermination();
    }

    tic_net_end(net);
    free(net->requests);
    free(net);
}
