#include "ns3/address.h"
#include "ns3/address-utils.h"
#include "ns3/log.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/node.h"
#include "ns3/socket.h"
#include "ns3/udp-socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "hotstuff-node.h"
#include "stdlib.h"
#include "ns3/ipv4.h"
#include <ctime>
#include <map>
#include <string>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("HotStuffNode");
NS_OBJECT_ENSURE_REGISTERED (HotStuffNode);

TypeId
HotStuffNode::GetTypeId (void)
{
    static TypeId tid = TypeId ("ns3::HotStuffNode")
    .SetParent<Application> ()
    .SetGroupName("Applications")
    .AddConstructor<HotStuffNode> ()
    ;
    return tid;
}

HotStuffNode::HotStuffNode(void) {
    currentView = 0;
    highQC = nullptr;
    lockedQC = nullptr;
    committedQC = nullptr;
    is_leader = false;
}

HotStuffNode::~HotStuffNode(void) {
    NS_LOG_FUNCTION (this);
}

void 
HotStuffNode::StartApplication ()
{
    // Initialize socket
    if (!m_socket)
    {
        TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
        m_socket = Socket::CreateSocket (GetNode (), tid);
        InetSocketAddress local = InetSocketAddress (Ipv4Address::GetAny (), 7071);
        m_socket->Bind (local);
        m_socket->Listen ();
    }

    m_socket->SetRecvCallback (MakeCallback (&HotStuffNode::HandleRead, this));
    m_socket->SetAllowBroadcast (true);

    // Connect to peers
    std::vector<Ipv4Address>::iterator iter = m_peersAddresses.begin();
    while(iter != m_peersAddresses.end()) {
        TypeId tid = TypeId::LookupByName ("ns3::UdpSocketFactory");
        Ptr<Socket> socketClient = Socket::CreateSocket (GetNode (), tid);
        socketClient->Connect (InetSocketAddress(*iter, 7071));
        m_peersSockets[*iter] = socketClient;
        iter++;
    }

    // Set initial leader
    if (m_id == 0) {
        is_leader = true;
    }
}

void 
HotStuffNode::HandleRead (Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address from;
    Address localAddress;

    while ((packet = socket->RecvFrom (from)))
    {
        if (packet->GetSize () == 0)
        {
            break;
        }

        if (InetSocketAddress::IsMatchingType (from))
        {
            std::string msg = getPacketContent(packet, from);
            uint8_t type = msg[0] - '0';

            switch (type)
            {
                case NEW_VIEW:
                {
                    // Handle new view message
                    QC_t* qc = deserializeQC(msg.substr(1));
                    OnReceiveNewView(qc);
                    break;
                }
                case PREPARE:
                {
                    // Handle prepare message
                    Node_t* node = deserializeNode(msg.substr(1));
                    OnReceiveProposal(node);
                    break;
                }
                case PRECOMMIT:
                case COMMIT:
                {
                    // Handle vote messages
                    std::string vote = msg.substr(1, 64);
                    Node_t* node = deserializeNode(msg.substr(65));
                    OnReceiveVote(vote, node);
                    break;
                }
                case DECIDE:
                {
                    // Handle decide message
                    Node_t* node = deserializeNode(msg.substr(1));
                    UpdateHighQC(&node->justify);
                    if (SafeNode(node, &node->justify)) {
                        committedQC = node;
                        ExecuteCommand(node->command);
                    }
                    break;
                }
            }
        }
    }
}

void 
HotStuffNode::StopApplication ()
{
    // printVector(values);
} 

bool 
HotStuffNode::SafeNode(HotStuffNode::Node_t* node, HotStuffNode::QC_t* qc)
{
    // Safety rule: accept if node extends from locked node
    if (lockedQC == nullptr || IsAncestor(node, lockedQC)) {
        return true;
    }
    // Liveness rule: accept if qc has higher view than locked qc
    return qc->view > lockedQC->justify.view;
}

void 
HotStuffNode::UpdateHighQC(QC_t* qc)
{
    if (highQC == nullptr || qc->view > highQC->justify.view) {
        highQC = nodes[qc->node_hash];
    }
}

void 
HotStuffNode::OnReceiveProposal(Node_t* node)
{
    if (SafeNode(node, &node->justify)) {
        // Vote for the proposal
        std::string vote = CreateVote(node);
        Send((uint8_t*)vote.c_str(), vote.length());
        
        // Update local state
        UpdateHighQC(&node->justify);
        nodes[node->hash] = node;
        
        // Update locked QC if needed
        if (IsPreCommitQC(&node->justify)) {
            lockedQC = nodes[node->justify.node_hash];
        }
    }
}

HotStuffNode::Node_t* 
HotStuffNode::deserializeNode(const std::string& data)
{
    // Format: hash|parent_hash|command|height|justify
    HotStuffNode::Node_t* node = new HotStuffNode::Node_t();
    size_t pos = 0;
    size_t next = data.find('|');
    
    if (next != std::string::npos) {
        node->hash = data.substr(pos, next - pos);
        pos = next + 1;
        next = data.find('|', pos);
        
        if (next != std::string::npos) {
            node->parent_hash = data.substr(pos, next - pos);
            pos = next + 1;
            next = data.find('|', pos);
            
            if (next != std::string::npos) {
                node->command = data.substr(pos, next - pos);
                pos = next + 1;
                next = data.find('|', pos);
                
                if (next != std::string::npos) {
                    node->height = std::stoi(data.substr(pos, next - pos));
                    pos = next + 1;
                    node->justify = *deserializeQC(data.substr(pos));
                }
            }
        }
    }
    return node;
}

HotStuffNode::QC_t* 
HotStuffNode::deserializeQC(const std::string& data)
{
    // Format: view|height|node_hash|sig1,sig2,...
    HotStuffNode::QC_t* qc = new HotStuffNode::QC_t();
    size_t pos = 0;
    size_t next = data.find('|');
    
    if (next != std::string::npos) {
        qc->view = std::stoi(data.substr(pos, next - pos));
        pos = next + 1;
        next = data.find('|', pos);
        
        if (next != std::string::npos) {
            qc->height = std::stoi(data.substr(pos, next - pos));
            pos = next + 1;
            next = data.find('|', pos);
            
            if (next != std::string::npos) {
                qc->node_hash = data.substr(pos, next - pos);
                pos = next + 1;
                
                // Parse signatures
                std::string sigs = data.substr(pos);
                size_t sig_pos = 0;
                size_t sig_next = sigs.find(',');
                while (sig_next != std::string::npos) {
                    qc->signatures.push_back(sigs.substr(sig_pos, sig_next - sig_pos));
                    sig_pos = sig_next + 1;
                    sig_next = sigs.find(',', sig_pos);
                }
                qc->signatures.push_back(sigs.substr(sig_pos));
            }
        }
    }
    return qc;
}

void 
HotStuffNode::ExecuteCommand(const std::string& command)
{
    NS_LOG_INFO("Node " << m_id << " executing command: " << command);
    // Actual command execution would be implemented here
    // For simulation purposes, we just log the command
}

bool 
HotStuffNode::IsAncestor(Node_t* descendant, Node_t* ancestor)
{
    if (descendant == nullptr || ancestor == nullptr) {
        return false;
    }
    
    Node_t* current = descendant;
    while (current != nullptr) {
        if (current->hash == ancestor->hash) {
            return true;
        }
        if (current->parent_hash.empty()) {
            break;
        }
        current = nodes[current->parent_hash];
    }
    return false;
}

std::string 
HotStuffNode::CreateVote(Node_t* node)
{
    // Create a vote message: type|signature|serializedNode
    std::string vote = std::to_string(PRECOMMIT);
    vote += "|" + std::to_string(m_id) + "_sig"; // In practice, this would be a real signature
    vote += "|" + serializeNode(node);
    return vote;
}

bool 
HotStuffNode::IsPreCommitQC(QC_t* qc)
{
    // Check if this QC represents a pre-commit phase completion
    // In HotStuff, this means the QC should have n-f valid signatures
    // and be from the prepare phase
    return qc != nullptr && qc->signatures.size() >= (2 * n_replicas / 3 + 1);
}

std::string 
HotStuffNode::serializeNode(Node_t* node)
{
    if (node == nullptr) {
        return "";
    }
    
    std::string serialized = node->hash + "|" +
                            node->parent_hash + "|" +
                            node->command + "|" +
                            std::to_string(node->height) + "|" +
                            serializeQC(&node->justify);
    return serialized;
}

std::string 
HotStuffNode::serializeQC(QC_t* qc)
{
    if (qc == nullptr) {
        return "";
    }
    
    std::string serialized = std::to_string(qc->view) + "|" +
                            std::to_string(qc->height) + "|" +
                            qc->node_hash + "|";
    
    // Add signatures
    for (size_t i = 0; i < qc->signatures.size(); i++) {
        if (i > 0) serialized += ",";
        serialized += qc->signatures[i];
    }
    
    return serialized;
}

std::string 
HotStuffNode::getPacketContent(Ptr<Packet> packet, Address from)
{
    char *packetInfo = new char[packet->GetSize () + 1];
    std::ostringstream totalStream;
    packet->CopyData (reinterpret_cast<uint8_t*>(packetInfo), packet->GetSize ());
    packetInfo[packet->GetSize ()] = '\0';
    totalStream << m_bufferedData[from] << packetInfo;
    return std::string(totalStream.str());
}

void 
HotStuffNode::Send(uint8_t* data, int size)
{
    Ptr<Packet> p = Create<Packet> (data, size);
    std::vector<Ipv4Address>::iterator iter = m_peersAddresses.begin();
    while(iter != m_peersAddresses.end()) {
        Ptr<Socket> socketClient = m_peersSockets[*iter];
        socketClient->Send(p);
        iter++;
    }
}

void 
HotStuffNode::Send(uint8_t* data, int size, Address from)
{
    Ptr<Packet> p = Create<Packet> (data, size);
    Ptr<Socket> socketClient = m_peersSockets[InetSocketAddress::ConvertFrom(from).GetIpv4 ()];
    socketClient->Send(p);
}

void 
HotStuffNode::OnReceiveNewView(HotStuffNode::QC_t* qc)
{
    if (is_leader) {
        // Update highest QC if the received QC has a higher view
        if (qc != nullptr && (highQC == nullptr || qc->view > highQC->justify.view)) {
            UpdateHighQC(qc);
            
            // Create a new proposal extending from highQC
            HotStuffNode::Node_t* newNode = new HotStuffNode::Node_t();
            newNode->parent_hash = highQC->hash;  // Changed from node_hash to hash
            newNode->height = highQC->height + 1;
            newNode->justify = *qc;
            
            // Generate hash for new node
            newNode->hash = std::to_string(currentView) + "_" + newNode->parent_hash;  // Simple hash generation
            
            // Broadcast prepare message
            std::string proposal = serializeNode(newNode);
            std::string msg = std::to_string(PREPARE) + proposal;
            Send((uint8_t*)msg.c_str(), msg.length());
        }
    }
}

void 
HotStuffNode::OnReceiveVote(std::string vote, HotStuffNode::Node_t* node)
{
    if (is_leader) {
        // Add vote to the collection
        if (nodes.find(node->hash) == nodes.end()) {
            nodes[node->hash] = node;
        }
        
        // Count votes for this node
        Node_t* votedNode = nodes[node->hash];
        votedNode->justify.signatures.push_back(vote);
        
        // Check if we have enough votes for next phase
        if (votedNode->justify.signatures.size() >= (2 * n_replicas / 3 + 1)) {
            // Create new QC and broadcast next phase message
            std::string msg;
            if (votedNode->justify.view % 3 == 0) {
                // Prepare phase complete, broadcast pre-commit
                msg = std::to_string(PRECOMMIT) + serializeNode(votedNode);
            } else if (votedNode->justify.view % 3 == 1) {
                // Pre-commit phase complete, broadcast commit
                msg = std::to_string(COMMIT) + serializeNode(votedNode);
            } else {
                // Commit phase complete, broadcast decide
                msg = std::to_string(DECIDE) + serializeNode(votedNode);
                // Execute the command
                ExecuteCommand(votedNode->command);
            }
            Send((uint8_t*)msg.c_str(), msg.length());
        }
    }
}

} // namespace ns3