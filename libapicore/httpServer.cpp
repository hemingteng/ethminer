#include <limits.h>
#include <chrono>
#include <thread>

#include <mongoose/mongoose.h>

#include "ethminer/buildinfo.h"
#include "httpServer.h"
#include "libdevcore/Common.h"
#include "libdevcore/Log.h"

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 255
#endif

using namespace dev;
using namespace eth;

httpServer http_server;

void httpServer::tableHeader(stringstream& ss, unsigned columns)
{
    char hostName[HOST_NAME_MAX + 1];
    gethostname(hostName, HOST_NAME_MAX + 1);
    string l = m_farm->farmLaunchedFormatted();
    ss << "<html><head><title>" << hostName
       << "</title><style>tr:nth-child(even){background-color:Gainsboro;}</style>"
          "<meta http-equiv=refresh content=30></head><body><table width=\"50%\" border=1 "
          "cellpadding=2 cellspacing=0 align=center>"
          "<tr valign=top align=center style=background-color:Gold><th colspan="
       << columns << ">" << ethminer_get_buildinfo()->project_name_with_version << " on "
       << hostName << " - " << l << "<br/>Pool: " << m_manager->getActiveConnectionCopy().Host()
       << "</th></tr>";
}


void httpServer::getstat1(stringstream& ss)
{
    using namespace std::chrono;
    WorkingProgress p = m_farm->miningProgress();
    SolutionStats s = m_farm->getSolutionStats();
    tableHeader(ss, 6);
    ss << "<tr valign=top align=center style=background-color:Yellow>"
          "<th>GPU</th>"
          "<th>Hash Rate (MH/s)</th>"
          "<th>Solutions</th>"
          "<th>Temperature (C)</th>"
          "<th>Fan Percent.</th>"
          "<th>Power (W)</th></tr>";
    double hashSum = 0.0;
    double powerSum = 0.0;
    for (unsigned i = 0; i < p.minersHashRates.size(); i++)
    {
        double rate = p.minersHashRates[i] / 1000000.0;

        hashSum += rate;
        ss << "<tr valign=top align=center><td";
        if (i < p.miningIsPaused.size() && p.miningIsPaused[i])
            ss << " style=color:Red";
        ss << ">" << i << "</td><td>" << fixed << setprecision(2) << rate;
        ss << "</td><td>" << s.getString(i);
        if (m_show_hwmonitors && (i < p.minerMonitors.size()))
        {
            HwMonitor& hw(p.minerMonitors[i]);
            powerSum += hw.powerW;
            ss << "</td><td>" << hw.tempC << "</td><td>" << hw.fanP << "</td><td>";
            if (m_show_power)
                ss << fixed << setprecision(0) << hw.powerW;
            else
                ss << '-';
            ss << "</td></tr>";
        }
        else
            ss << "</td><td>-</td><td>-</td><td>-</td></tr>";
    }
    ss << "<tr valign=top align=center style=\"background-color:yellow\"><th>Total</th><td>"
       << fixed << setprecision(2) << hashSum << "</td><td colspan=3>Solutions: " << s
       << "</td><td>";
    if (m_show_power)
        ss << fixed << setprecision(0) << powerSum;
    else
        ss << '-';
    ss << "</td></tr></table></body></html>";
}

static void ev_handler(struct mg_connection* c, int ev, void* p)
{
    if (ev == MG_EV_HTTP_REQUEST)
    {
        struct http_message* hm = (struct http_message*)p;
        if (mg_vcmp(&hm->uri, "/getstat1") && mg_vcmp(&hm->uri, "/"))
            mg_http_send_error(c, 404, nullptr);
        else
        {
            stringstream content;
            http_server.getstat1(content);
            mg_send_head(c, 200, (int)content.str().length(), "Content-Type: text/html; charset=utf-8");
            mg_printf(c, "%.*s", (int)content.str().length(), content.str().c_str());
        }
    }
}

void httpServer::run(string address, uint16_t port, dev::eth::Farm* farm, PoolManager* manager,
    bool show_hwmonitors, bool show_power)
{
    if (port == 0)
        return;
    m_farm = farm;
    m_manager = manager;
    // admittedly, at this point, it's a bit hacky to call it "m_port" =/
    if (address.empty())
    {
        m_port = to_string(port);
    }
    else
    {
        m_port = address + string(":") + to_string(port);
    }
    m_show_hwmonitors = show_hwmonitors;
    m_show_power = show_power;
    new thread(bind(&httpServer::run_thread, this));
}

void httpServer::run_thread()
{
    struct mg_mgr mgr;
    struct mg_connection* c;

    mg_mgr_init(&mgr, nullptr);
    cnote << "Starting web server on port " << m_port;
    c = mg_bind(&mgr, m_port.c_str(), ev_handler);
    if (c == nullptr)
    {
        cwarn << "Failed to create web listener";
        return;
    }

    // Set up HTTP server parameters
    mg_set_protocol_http_websocket(c);

    for (;;)
        mg_mgr_poll(&mgr, 1000);
}
