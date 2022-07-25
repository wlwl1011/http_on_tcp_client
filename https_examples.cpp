#include "server_https.hpp"
#include "client_https.hpp"

// Added for the json-example
#define BOOST_SPIRIT_THREADSAFE
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

// Added for the default_resource example
#include <fstream>
#include <boost/filesystem.hpp>
#include <vector>
#include <algorithm>
#include "crypto.hpp"
// [SD] for address check
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <thread>
#include <stdlib.h>
#include <chrono>
#include <net/route.h>
#include <sys/types.h>
#include <sys/ioctl.h>
using std::cout;
using std::endl;
using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::seconds;
using std::chrono::system_clock;
using namespace std;
// Added for the json-example:
using namespace boost::property_tree;

typedef SimpleWeb::Server<SimpleWeb::HTTPS> HttpsServer;
typedef SimpleWeb::Client<SimpleWeb::HTTPS> HttpsClient;

// Time
size_t total_delay_end_time;
size_t total_delay_start_time;

size_t handover_delay_end_time;
size_t handover_delay_start_time;

// Added for the default_resource example
void default_resource_send(const HttpsServer &server, const shared_ptr<HttpsServer::Response> &response,
                           const shared_ptr<ifstream> &ifs);

void NetworkChange(std::string fromIface, std::string toIface)
{

    // std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::string del_order = "sudo route del -net 0.0.0.0 dev ";
    std::string add_order = "sudo route add -net 0.0.0.0 gw ";

    FILE *fp;
    char buf[256];
    static char iface[256];
    in_addr_t dest, gw, flag, refcnt, use, metric, mask;
    struct in_addr from_gw, to_gw;
    unsigned int to_metric;
    int ret;

    for (int i = 0; i < 1; i++)
    {

        fp = fopen("/proc/net/route", "r");

        if (fp == NULL)
            return;

        while (fgets(buf, 255, fp))
        {
            if (!strncmp(buf, "Iface", 5))
                continue;
            ret = sscanf(buf, "%s\t%x\t%x\t%d\t%d\t%d\t%d\t%x", iface, &dest, &gw, &flag, &refcnt, &use, &metric, &mask);

            if (ret < 8)
            {
                fprintf(stderr, "Line Read Error");
                return;
            }

            if (dest == 0)
            {
                if (strcmp(fromIface.c_str(), iface) == 0)
                {
                    from_gw.s_addr = gw;
                }
                else if (strcmp(toIface.c_str(), iface) == 0)
                {
                    to_gw.s_addr = gw;
                    to_metric = metric;
                }
            }
        }

        fclose(fp);

        // change network info

        system(std::string(del_order + fromIface).c_str());
        std::cout << "[quic_toy_client] Network Change from " << fromIface << " to " << toIface << std::endl;
        cout << std::string(add_order + inet_ntoa(to_gw) + " dev " + toIface + " metric 51").c_str() << endl;
        system(std::string(add_order + inet_ntoa(to_gw) + " dev " + toIface + " metric 51").c_str());
        auto millisec_since_epoch_hanover_start = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        handover_delay_start_time = millisec_since_epoch_hanover_start;
        cout << std::string(del_order + toIface + " metric " + std::to_string(to_metric)).c_str() << endl;
        system(std::string(del_order + toIface + " metric " + std::to_string(to_metric)).c_str());
        cout << std::string(add_order + inet_ntoa(from_gw) + " dev " + fromIface + " metric 600").c_str() << endl;
        system(std::string(add_order + inet_ntoa(from_gw) + " dev " + fromIface + " metric 600").c_str());
    }
}

// [SD] check addresses in network interfaces
void Watcher(std::shared_ptr<QuicClientBase> client, int *thread_kill)
{

   
    char new_iface[64], before_iface[64];
    in_addr_t dest, gway, mask;
    int flags, refcnt, use, metric, mtu, win, irtt;
    int first_try = 0;
    FILE *fp;

    while (*thread_kill == 0)
    {
        if ((fp = fopen("/proc/net/route", "r")) == NULL)
        {
            perror("file open error");
            return;
        }

        if (fscanf(fp, "%*[^\n]\n") < 0)
        {
            fclose(fp);
            return;
        }

        for (;;)
        {
            int nread = fscanf(fp, "%63s%X%X%X%d%d%d%X%d%d%d\n",
                               new_iface, &dest, &gway, &flags, &refcnt, &use, &metric, &mask,
                               &mtu, &win, &irtt);
            if (nread != 11)
            {
                break;
            }
            if ((flags & (RTF_UP | RTF_GATEWAY)) == (RTF_UP | RTF_GATEWAY ) && dest == 0)
            {
                if (strcmp(before_iface, new_iface) != 0)
                {
                    if (first_try == 1)
                    {
                        // ho start time

                        // connection migration 시작
                        client->StartAddressChange(QuicIpAddress(IfaceToAddress(std::string(before_iface))), QuicIpAddress(IfaceToAddress(std::string(new_iface))));
                        std::cout << "[quic_toy_client] Detected network change and Start connection migration from " << before_iface << " to " << new_iface << std::endl;
                        // std::cout << "[quic_toy_client] Start Address Change from " << IfaceToAddress(std::string(before_iface)) << ":" << client->local_port() << " to " << IfaceToAddress(std::string(new_iface)) << ":" << client->local_port() << std::endl;
                        // std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    // std::cout << before_iface << " vs " << new_iface << std::endl;
                    strcpy(before_iface, new_iface);
                    first_try = 1;
                }
                break;
            }
        }
        fclose(fp);
    }
    std::cout << "[quic_toy_client] watcher terminates.." << std::endl;
}

QuicIpAddress IfaceToAddress(std::string iface) {
  struct ifaddrs *ifap, *ifa;
  struct sockaddr_in *sa;
  QuicIpAddress ip = QuicIpAddress::Any4();

  getifaddrs(&ifap);
  for(ifa = ifap; ifa; ifa = ifa->ifa_next) {
    if(ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
      if(strcmp(ifa->ifa_name, iface.c_str()) == 0) {
        sa = (struct sockaddr_in *) ifa->ifa_addr;
        ip = QuicIpAddress(sa->sin_addr);
        break;
      }
    }
  }
  freeifaddrs(ifap);
  return ip;
}

void thread_function()
{

    this_thread::sleep_for(chrono::milliseconds(150));

    NetworkChange("eth0", "wlo1");
    HttpsClient client2("quic.server-2:8000", false);

    // auto r2 = client2.request("GET", "./index.html");
    // std::cout << r2->status_code << std::endl;

    handover_delay_end_time = client2.handover_delay_end_time;
    auto millisec_since_epoch = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    total_delay_end_time = millisec_since_epoch; // 2번 째 CLOSE에서 시간 측정

    // client2.close();

    std::cout << "handover delay : " << (double)(handover_delay_end_time - handover_delay_start_time) << " miliseconds" << std::endl;
    std::cout << "total delay : " << (double)(total_delay_end_time - total_delay_start_time) << " miliseconds" << std::endl;

    // system("../network_recovery.sh");
}

int main()
{

    HttpsClient client("quic.server-2:8000", false);
    thread _t1(thread_function);
    auto r1 = client.request("GET", "./index.html");
    std::cout << r1->status_code << std::endl;

    total_delay_start_time = client.total_delay_start_time; //첫 REQUEST에서 시작 재기 시작함ma
    std::cout << "1번 끝" << std::endl;
    _t1.join();

    return 0;
}

void default_resource_send(const HttpsServer &server, const shared_ptr<HttpsServer::Response> &response,
                           const shared_ptr<ifstream> &ifs)
{
    // read and send 128 KB at a time
    static vector<char> buffer(131072); // Safe when server is running on one thread
    streamsize read_length;
    if ((read_length = ifs->read(&buffer[0], buffer.size()).gcount()) > 0)
    {
        response->write(&buffer[0], read_length);
        if (read_length == static_cast<streamsize>(buffer.size()))
        {
            server.send(response, [&server, response, ifs](const boost::system::error_code &ec)
                        {
                if(!ec)
                    default_resource_send(server, response, ifs);
                else
                    cerr << "Connection interrupted" << endl; });
        }
    }
}
