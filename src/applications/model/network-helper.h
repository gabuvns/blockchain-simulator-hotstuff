#ifndef NETWORK_HELPER_H
#define NETWORK_HELPER_H

#include "ns3/application-container.h"
#include "ns3/node-container.h"
#include "ns3/ipv4-address.h"
#include "raft-node.h"
#include <vector>
#include <map>

namespace ns3 {

class NetworkHelper
{
public:
    NetworkHelper(int numNodes) : m_numNodes(numNodes) {}
    
    ApplicationContainer Install(NodeContainer nodes) {
        ApplicationContainer apps;
        
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            Ptr<PbftNode> app = CreateObject<PbftNode>();
            app->m_id = i;
            
            // Set peer addresses for this node
            std::vector<Ipv4Address> peerAddresses;
            if (m_nodesConnectionsIps.find(i) != m_nodesConnectionsIps.end()) {
                peerAddresses = m_nodesConnectionsIps[i];
            }
            app->SetPeersAddresses(peerAddresses);
            
            nodes.Get(i)->AddApplication(app);
            apps.Add(app);
        }
        
        return apps;
    }

    std::map<int, std::vector<Ipv4Address>> m_nodesConnectionsIps;

private:
    int m_numNodes;
};

} // namespace ns3

#endif