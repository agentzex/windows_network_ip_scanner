

#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <iphlpapi.h>
#include <thread>
#include <mutex>
#include "json.hpp"


#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

using namespace std;
using json = nlohmann::json;

//globals
json found_devices;
json interfaces;
mutex m;


void print_found() {
	cout << "Local Network Devices:" << endl;
	for (auto&[key, network_cards] : interfaces.items()) {
		cout << "Name: " << network_cards["name"] << endl;
		cout << "IP: " << network_cards["ip"] << endl;
		cout << "Subnet: " << network_cards["subnet"] << endl;
		cout << "****************************************************\n\n";
	}

	cout << "########################################################\n\n";
	cout << "Found Neighbor devices" << endl;

	for (auto&[key, l_interface] : found_devices.items()) {
		cout << "IP: " << l_interface["ip"] << endl;
		cout << "MAC Address: " << l_interface["mac"] << endl;
		cout << "Hostname: " << l_interface["hostname"] << endl;
		cout << "****************************************************\n\n";
	}
		
}


string get_subnet(string subnet_mask){
	//currently supporting only whole class A, B, or C private networks
	string sub;
	if (subnet_mask == "255.255.255.0")
		sub = "24";
	else if (subnet_mask == "255.255.0.0")
		sub = "16";
	else if (subnet_mask == "255.0.0.0")
		sub = "8";
	else
		sub = "0";
	return sub;
}


bool get_interfaces() {

	DWORD Err;
	PFIXED_INFO pFixedInfo;
	DWORD FixedInfoSize = 0;
	PIP_ADAPTER_INFO pAdapterInfo, pAdapt;
	DWORD AdapterInfoSize;
	PIP_ADDR_STRING pAddrStr;
	UINT i;
	struct tm newtime;
	char buffer[32];
	int error;

	// Get the main IP configuration information for this machine using a FIXED_INFO structure
	if ((Err = GetNetworkParams(NULL, &FixedInfoSize)) != 0)
	{
		if (Err != ERROR_BUFFER_OVERFLOW)
		{
			//printf("GetNetworkParams sizing failed with error %d\n", Err);
			return false;
		}
	}

	// Allocate memory from sizing information
	if ((pFixedInfo = (PFIXED_INFO)GlobalAlloc(GPTR, FixedInfoSize)) == NULL){
		//printf("Memory allocation error\n");
		return false;
	}

	// Enumerate all of the adapter specific information using the IP_ADAPTER_INFO structure.
	// Note:  IP_ADAPTER_INFO contains a linked list of adapter entries.
	AdapterInfoSize = 0;
	if ((Err = GetAdaptersInfo(NULL, &AdapterInfoSize)) != 0)
	{
		if (Err != ERROR_BUFFER_OVERFLOW)
		{
			//printf("GetAdaptersInfo sizing failed with error %d\n", Err);
			return false;
		}
	}

	// Allocate memory from sizing information
	if ((pAdapterInfo = (PIP_ADAPTER_INFO)GlobalAlloc(GPTR, AdapterInfoSize)) == NULL)
	{
		//printf("Memory allocation error\n");
		return false;
	}

	// Get actual adapter information
	if ((Err = GetAdaptersInfo(pAdapterInfo, &AdapterInfoSize)) != 0)
	{
		//printf("GetAdaptersInfo failed with error %d\n", Err);
		return false;
	}

	pAdapt = pAdapterInfo;
	while (pAdapt){
		json l_interface;
		l_interface["name"] = pAdapt->Description;
		pAddrStr = &(pAdapt->IpAddressList);
		l_interface["ip"] = pAddrStr->IpAddress.String;
		l_interface["subnet"] = pAddrStr->IpMask.String;
		string sub = get_subnet(l_interface["subnet"].get<std::string>());
		l_interface["sub"] = sub;
		interfaces.push_back(l_interface);
		pAdapt = pAdapt->Next;
	}
	return true;
}


void split_ip(string ip, vector<string> &splitted_ip) {
	//splits ip address to octets
	string delimiter = ".";
	size_t pos = 0;
	string token;
	while ((pos = ip.find(delimiter)) != string::npos) {
		token = ip.substr(0, pos);
		splitted_ip.push_back(token);
		ip.erase(0, pos + delimiter.length());
	}
	splitted_ip.push_back(ip);
}


void ip_to_mac(string DestIpString) {

	DWORD dwRetVal;
	int inetPtonRet;
	IPAddr DestIp = 0;
	IPAddr SrcIp = 0;       /* default for src ip */
	ULONG MacAddr[2];       /* for 6-byte hardware addresses */
	ULONG PhysAddrLen = 6;  /* default to length of six bytes */
	unsigned int i = 0;
	BYTE *bPhysAddr;
	char mac[20] = { 0 };

	inetPtonRet = InetPtonA(AF_INET, DestIpString.c_str(), &DestIp);
	if (inetPtonRet != 1) { //error
		return;
	}

	memset(&MacAddr, 0xff, sizeof(MacAddr));
	dwRetVal = SendARP(DestIp, SrcIp, &MacAddr, &PhysAddrLen); // if mac address returned then this ip is alive, so we'll add it to the found_devices

	if (dwRetVal == NO_ERROR) {
		bPhysAddr = (BYTE *)& MacAddr;
		if (PhysAddrLen) {
			for (i = 0; i < (int)PhysAddrLen; i++) {
				if (i == (PhysAddrLen - 1)) {
					snprintf(mac + (i * 3), 15, "%.2X", (int)bPhysAddr[i]);
				}
				else {
					snprintf(mac + (i * 3), 15, "%.2X-", (int)bPhysAddr[i]);
				}
			}
			json new_interface;
			new_interface["ip"] = DestIpString;
			new_interface["mac"] = mac;
			m.lock();
				found_devices[DestIpString] = new_interface;
			m.unlock();
		}
	}
}



void scan_range(string start_ip) {
	//sends arp requests to each of the hosts in the subnet
	string current_ip;
	vector<std::thread> threads;

	for (int j = 1; j < 255; j++) {
		current_ip = start_ip + "." + to_string(j);
		threads.push_back(thread(ip_to_mac, current_ip));  
	}

	for (auto& t : threads) {
		t.join();
	}
}


void get_hostname(string ip) {

	struct addrinfo *res = NULL;
	if (getaddrinfo(ip.c_str(), NULL, NULL, &res) != 0) {
		freeaddrinfo(res);
		found_devices[ip]["hostname"] = "";
		return;
	}
	char host[512];
	if (getnameinfo(res->ai_addr, res->ai_addrlen, host, 512, 0, 0, 0) != 0) {
		freeaddrinfo(res);
		found_devices[ip]["hostname"] = "";
		return;
	}

	freeaddrinfo(res);
	found_devices[ip]["hostname"] = host;
}


int main(int argc, char **argv)
{
	int i, k;
	if (!get_interfaces()) {
		return -1;
	}

	for (auto&[key, l_interface] : interfaces.items()) {
		vector<string> splitted_ip;
		string start_ip = "";
		string final_ip = "";


		if (l_interface["ip"].get<std::string>().rfind("169.254", 0) == 0) { //skipping apipa
			continue;
		}

		split_ip(l_interface["ip"].get<std::string>(), splitted_ip);

		if (l_interface["sub"] == "24") {
			start_ip = splitted_ip[0] + '.' + splitted_ip[1] + '.' + splitted_ip[2];
			scan_range(start_ip);
		}
		else if (l_interface["sub"] == "16") {
			start_ip = splitted_ip[0] + '.' + splitted_ip[1];
			for (i = 0; i < 256; i++) {
				final_ip = start_ip + "." + to_string(i);
				scan_range(final_ip);
			}
		}
		else if (l_interface["sub"] == "8") {
			start_ip = splitted_ip[0];
			for (k = 0; k < 256; k++) {
				for (i = 0; i < 256; i++) {
					final_ip = start_ip + "." + to_string(k) + "." + to_string(i);
					scan_range(final_ip);
				}
			}
		}
	}

	//adding hostname
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
		vector<std::thread> threads;
		for (auto& [ip, l_interface] : found_devices.items()) {
			threads.push_back(thread(get_hostname, ip));
		}
		for (auto& t : threads) {
			t.join();
		}
	}

	print_found();
	return 0;
}
