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

// Initialize static members
int HotStuffNode::tx_size = 256;  // Reduced from 4KB to 1KB default transaction size
double HotStuffNode::network_delay = 0.001;  // Keeping the original 1ms network delay
int tx_speed = 10000; //tps
double timeout = 0.05;  
int num = tx_speed / (1000 / (timeout * 1000)); 

int totalSize = num * HotStuffNode::tx_size;
HotStuffNode::HotStuffNode(void) {
    currentView = 0;
    currentPhase = 0;
    highQC = nullptr;
    lockedQC = nullptr;
    committedQC = nullptr;
    is_leader = false;
    consensusReached = false;
    
    // Initialize benchmarking counters
    messages_sent = 0;
    messages_received = 0;
    total_latency = 0.0;
}

bool isNumeric(const std::string& str) {
    // Check if string is empty
    if (str.empty()) {
        return false;
    }

    // Iterate through each character
    for (char const &c : str) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    
    return true;
}

void 
HotStuffNode::LogMessageSent(const std::string& msg_id) 
{
    messages_sent++;
    message_timestamps[msg_id] = Simulator::Now().GetSeconds();
}

void 
HotStuffNode::LogMessageReceived(const std::string& msg_id) 
{
    messages_received++;
    auto sent_time = message_timestamps[msg_id];
    if (sent_time > 0) {
        double latency = Simulator::Now().GetSeconds() - sent_time;
        total_latency += latency;
        NS_LOG_INFO("Message " << msg_id << " latency: " << latency << "s");
    }
}

double 
HotStuffNode::GetAverageLatency() const 
{
    if (messages_received == 0) return 0;
    return total_latency / messages_received;
}

uint32_t 
HotStuffNode::GetMessageCount() const 
{
    return messages_sent;
}

HotStuffNode::~HotStuffNode(void) {
    NS_LOG_FUNCTION (this);
}

void 
HotStuffNode::StartApplication ()
{
    // Record start time for consensus
    consensusStartTime = Simulator::Now();
    phaseStartTime = Simulator::Now();
    
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
        NS_LOG_INFO("Node " << m_id << " is the initial leader");
        
        // Create a simple command for the first proposal
        std::string command = "initial_command";
        
        // Create initial node
        Node_t* initialNode = new Node_t();
        initialNode->hash = "initial_hash";
        initialNode->parent_hash = "";
        initialNode->command = command;
        initialNode->height = 0;
        initialNode->justify.view = 0;
        initialNode->justify.height = 0;
        initialNode->justify.node_hash = "";
        
        // Store the node
        nodes[initialNode->hash] = initialNode;
        
        // Broadcast initial prepare message
        std::string proposal = std::to_string(PREPARE) + serializeNode(initialNode);
        Send((uint8_t*)proposal.c_str(), proposal.length());
        
        // Log message
        NS_LOG_INFO("Leader started consensus with initial proposal");
    }
}

int
HotStuffNode::CalculatePacketSize(const std::string& data)
{
    // Calculate actual size needed (don't use more than tx_size)
    return std::min((int)data.length(), tx_size);
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
            
            // Check if consensus already reached - skip processing if done
            // if (consensusReached) {
            //     NS_LOG_INFO("Node " << m_id << " already reached consensus, ignoring message");
            //     continue;
            // }
            
            // Parse message type
            if (msg.empty()) {
                NS_LOG_WARN("Received empty message, skipping");
                continue;
            }
            
            uint8_t type = msg[0] - '0';
            std::string msgId = "msg_" + std::to_string(type) + "_" + std::to_string(messages_received);
            LogMessageReceived(msgId);

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
                {
                    // Handle pre-commit message
                    Node_t* node = deserializeNode(msg.substr(1));
                    OnReceivePreCommit(node);
                    break;
                }
                case COMMIT:
                {
                    // Handle vote messages
                    size_t voteEnd = msg.find('|', 1);
                    if (voteEnd == std::string::npos) {
                        NS_LOG_WARN("Malformed COMMIT message, skipping");
                        break;
                    }
                    std::string vote = msg.substr(1, voteEnd - 1);
                    Node_t* node = deserializeNode(msg.substr(voteEnd + 1));
                    OnReceiveVote(vote, node);
                    break;
                }
                case DECIDE:
                {
                    // Handle decide message
                    Node_t* node = deserializeNode(msg.substr(1));
                    OnReceiveDecide(node);
                    break;
                }
                default:
                    NS_LOG_WARN("Unknown message type: " << type);
                    break;
            }
        }
    }
}

void
HotStuffNode::OnReceiveDecide(Node_t* node)
{
    UpdateHighQC(&node->justify);
    if (SafeNode(node, &node->justify)) {
        // Mark this node as committed
        committedQC = node;
        
        // Execute the command
        ExecuteCommand(node->command);
        
        // Signal consensus completion
        consensusReached = true;
        
        // Log consensus success with timing information
        Time consensusTime = Simulator::Now() - consensusStartTime;
        NS_LOG_INFO("Node " << m_id << " reached consensus after " 
                    << consensusTime.GetSeconds() << "s");
        NS_LOG_INFO("Node " << m_id << " message count: " << messages_sent 
                    << ", avg latency: " << GetAverageLatency() << "s");
        
        // Optional: Stop receiving further messages
        if (m_socket) {
            m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
        }
    }
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
HotStuffNode::StopApplication(){}

void 
HotStuffNode::OnReceiveProposal(Node_t* node)
{
    // Log phase timing
    Time phaseTime = Simulator::Now() - phaseStartTime;
    NS_LOG_INFO("Node " << m_id << " prepare phase took " << phaseTime.GetSeconds() << "s");
    phaseStartTime = Simulator::Now();
    
    if (SafeNode(node, &node->justify)) {
        // Store the node
        nodes[node->hash] = node;
        
        // Update local state
        UpdateHighQC(&node->justify);
        
        // Vote for the proposal
        std::string vote = CreateVote(node);
        
        // Add to pending votes for batching
        pendingVotes.push_back(vote);
        
        // Send immediately if batch size reached or we're in a small network
        if (pendingVotes.size() >= BATCH_SIZE || n_replicas < BATCH_SIZE) {
            SendBatchedVotes();
        }
        
        // Update locked QC if needed
        if (IsPreCommitQC(&node->justify)) {
            lockedQC = nodes[node->justify.node_hash];
        }
    } else {
        NS_LOG_WARN("Node " << m_id << " rejected unsafe proposal");
    }
}

void
HotStuffNode::SendBatchedVotes()
{
    if (pendingVotes.empty()) {
        return;
    }
    
    // Combine votes into a single message
    std::string batchedVotes = std::to_string(COMMIT);
    for (const auto& vote : pendingVotes) {
        batchedVotes += vote;
    }
    
    // Send the batched votes
    Send((uint8_t*)batchedVotes.c_str(), batchedVotes.length());
    
    // Log the batched message
    std::string msgId = "batched_votes_" + std::to_string(messages_sent);
    LogMessageSent(msgId);
    
    // Clear the pending votes
    pendingVotes.clear();
}

void
HotStuffNode::BroadcastForCurrentPhase(Node_t* node)
{
    std::string msg;
    
    switch (currentPhase) {
        case 0: // Prepare phase
            msg = std::to_string(PREPARE) + serializeNode(node);
            break;
        case 1: // Pre-commit phase
            msg = std::to_string(PRECOMMIT) + serializeNode(node);
            break;
        case 2: // Commit phase
            msg = std::to_string(COMMIT) + serializeNode(node);
            break;
        default: // Decide phase
            msg = std::to_string(DECIDE) + serializeNode(node);
            break;
    }
    
    Send((uint8_t*)msg.c_str(), msg.length());
    
    // Log the broadcast
    std::string msgId = "phase_" + std::to_string(currentPhase) + "_" + node->hash;
    LogMessageSent(msgId);
}

HotStuffNode::Node_t* 
HotStuffNode::deserializeNode(const std::string& data)
{
    // Improved deserialize method with better error checking
    HotStuffNode::Node_t* node = new HotStuffNode::Node_t();
    
    try {
        size_t pos = 0;
        size_t next = data.find('|');
        
        if (next == std::string::npos) {
            NS_LOG_WARN("Malformed node data: missing first delimiter");
            delete node;
            return nullptr;
        }
        
        node->hash = data.substr(pos, next - pos);
        pos = next + 1;
        next = data.find('|', pos);
        
        if (next == std::string::npos) {
            NS_LOG_WARN("Malformed node data: missing second delimiter");
            delete node;
            return nullptr;
        }
        
        node->parent_hash = data.substr(pos, next - pos);
        pos = next + 1;
        next = data.find('|', pos);
        
        if (next == std::string::npos) {
            NS_LOG_WARN("Malformed node data: missing third delimiter");
            delete node;
            return nullptr;
        }
        
        node->command = data.substr(pos, next - pos);
        pos = next + 1;
        next = data.find('|', pos);
        
        if (next == std::string::npos) {
            NS_LOG_WARN("Malformed node data: missing fourth delimiter");
            delete node;
            return nullptr;
        }
        
        // More robust height parsing
        std::string heightStr = data.substr(pos, next - pos);
        pos = next + 1;
        
        // Find first digit in height string
        size_t digitPos = 0;
        while (digitPos < heightStr.size() && !isdigit(heightStr[digitPos])) {
            digitPos++;
        }
        
        if (digitPos < heightStr.size()) {
            node->height = std::stoi(heightStr.substr(digitPos));
        } else {
            NS_LOG_WARN("Malformed node data: invalid height");
            node->height = 0;
        }
        
        // Parse justify QC
        QC_t* qc = deserializeQC(data.substr(pos));
        if (qc != nullptr) {
            node->justify = *qc;
            delete qc;
        }
    } catch (const std::exception& e) {
        NS_LOG_ERROR("Exception in deserializeNode: " << e.what());
        delete node;
        return nullptr;
    }
    
    return node;
}

HotStuffNode::QC_t* 
HotStuffNode::deserializeQC(const std::string& data)
{
    // Improved deserialize method with better error checking
    HotStuffNode::QC_t* qc = new HotStuffNode::QC_t();
    
    try {
        size_t pos = 0;
        size_t next = data.find('|');
        
        if (next == std::string::npos) {
            NS_LOG_WARN("Malformed QC data: missing first delimiter");
            delete qc;
            return nullptr;
        }
        
        // Parse view number
        std::string viewStr = data.substr(pos, next - pos);
        
        // Find first digit in view string
        size_t digitPos = 0;
        while (digitPos < viewStr.size() && !isdigit(viewStr[digitPos])) {
            digitPos++;
        }
        
        if (digitPos < viewStr.size()) {
            qc->view = std::stoi(viewStr.substr(digitPos));
        } else {
            NS_LOG_WARN("Malformed QC data: invalid view");
            qc->view = 0;
        }
        
        pos = next + 1;
        next = data.find('|', pos);
        
        if (next == std::string::npos) {
            NS_LOG_WARN("Malformed QC data: missing second delimiter");
            delete qc;
            return nullptr;
        }
        
        // Parse height number
        std::string heightStr = data.substr(pos, next - pos);
        
        // Find first digit in height string
        digitPos = 0;
        while (digitPos < heightStr.size() && !isdigit(heightStr[digitPos])) {
            digitPos++;
        }
        
        if (digitPos < heightStr.size()) {
            qc->height = std::stoi(heightStr.substr(digitPos));
        } else {
            NS_LOG_WARN("Malformed QC data: invalid height");
            qc->height = 0;
        }
        
        pos = next + 1;
        next = data.find('|', pos);
        
        if (next == std::string::npos) {
            NS_LOG_WARN("Malformed QC data: missing third delimiter");
            delete qc;
            return nullptr;
        }
        
        qc->node_hash = data.substr(pos, next - pos);
        pos = next + 1;
        
        // Parse signatures
        std::string sigs = data.substr(pos);
        qc->signatures.clear();
        
        size_t sig_pos = 0;
        size_t sig_next = sigs.find(',');
        while (sig_next != std::string::npos) {
            qc->signatures.push_back(sigs.substr(sig_pos, sig_next - sig_pos));
            sig_pos = sig_next + 1;
            sig_next = sigs.find(',', sig_pos);
        }
        
        // Add the last signature
        if (sig_pos < sigs.size()) {
            qc->signatures.push_back(sigs.substr(sig_pos));
        }
    } catch (const std::exception& e) {
        NS_LOG_ERROR("Exception in deserializeQC: " << e.what());
        delete qc;
        return nullptr;
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
    
    // Use a depth limit to prevent infinite loops
    const int MAX_DEPTH = 100;
    int depth = 0;
    
    Node_t* current = descendant;
    while (current != nullptr && depth < MAX_DEPTH) {
        if (current->hash == ancestor->hash) {
            return true;
        }
        if (current->parent_hash.empty()) {
            break;
        }
        
        // Check if parent exists in nodes map
        auto it = nodes.find(current->parent_hash);
        if (it == nodes.end()) {
            break;
        }
        
        current = it->second;
        depth++;
    }
    
    return false;
}

std::string 
HotStuffNode::CreateVote(Node_t* node)
{
    // Create a vote message: signature|serializedNode
    std::string vote = std::to_string(m_id) + "_sig"; // Simpler signature
    vote += "|" + serializeNode(node);
    return vote;
}

bool 
HotStuffNode::IsPreCommitQC(QC_t* qc)
{
    // Check if this QC represents a pre-commit phase completion
    // In HotStuff, this means the QC should have n-f valid signatures
    // and be from the prepare phase
    int quorumSize = (2 * n_replicas / 3 + 1);
    return qc != nullptr && qc->signatures.size() >= quorumSize;
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
    char *packetInfo = new char[packet->GetSize() + 1];
    packet->CopyData(reinterpret_cast<uint8_t*>(packetInfo), packet->GetSize());
    packetInfo[packet->GetSize()] = '\0';
    
    // Combine with any buffered data
    std::string result = m_bufferedData[from] + packetInfo;
    // Clear the buffer after reading
    m_bufferedData[from] = "";
    
    delete[] packetInfo;
    return result;
}

// Initialize static parameters at file scope
//int tx_size = 200;                // size of tx in KB
//double network_delay = 0.1;        // network delay in seconds

void 
SendPacket(Ptr<Socket> socketClient, Ptr<Packet> p) {
    socketClient->Send(p);
}

void 
HotStuffNode::Send(uint8_t* data, int size)
{
    // Calculate actual size needed (rather than padding everything to tx_size)
    int actual_size = std::min(size, tx_size);
    
    // Create packet of appropriate size
    uint8_t* packet_data = new uint8_t[actual_size];
    
    // Copy original data
    memcpy(packet_data, data, std::min(size, actual_size));

    // Pad remaining space if needed
    if (size < actual_size) {
        memset(packet_data + size, '0', actual_size - size);
    }
    
    Ptr<Packet> p = Create<Packet>(packet_data, actual_size);
    
    // Send to all peers with network delay
    std::vector<Ipv4Address>::iterator iter = m_peersAddresses.begin();
    while(iter != m_peersAddresses.end()) {
        Ptr<Socket> socketClient = m_peersSockets[*iter];
        // Schedule send with network delay using ns3 Simulator
        Simulator::Schedule(Seconds(network_delay), SendPacket, socketClient, p);
        iter++;
    }
    
    // Log message sent
    std::string msgId = "msg_" + std::to_string(messages_sent);
    LogMessageSent(msgId);
    
    delete[] packet_data;
}

void 
HotStuffNode::Send(uint8_t* data, int size, Address from)
{
    // Calculate actual size needed (rather than padding everything to tx_size)
    int actual_size = std::min(size, tx_size);
    
    // Create packet of appropriate size
    uint8_t* packet_data = new uint8_t[actual_size];
    
    // Copy original data
    memcpy(packet_data, data, std::min(size, actual_size));

    // Pad remaining space if needed
    if (size < actual_size) {
        memset(packet_data + size, '0', actual_size - size);
    }
    
    Ptr<Packet> p = Create<Packet>(packet_data, actual_size);
    Ptr<Socket> socketClient = m_peersSockets[InetSocketAddress::ConvertFrom(from).GetIpv4()];
    
    // Schedule send with network delay using ns3 Simulator
    Simulator::Schedule(Seconds(network_delay), SendPacket, socketClient, p);
    
    // Log message sent
    std::string msgId = "msg_" + std::to_string(messages_sent);
    LogMessageSent(msgId);
    
    delete[] packet_data;
}

void 
HotStuffNode::OnReceiveNewView(HotStuffNode::QC_t* qc)
{
    NS_LOG_INFO("Node " << m_id << " received NEW_VIEW message");
    
    if (is_leader) {
        // Update highest QC if the received QC has a higher view
        if (qc != nullptr && (highQC == nullptr || qc->view > highQC->justify.view)) {
            UpdateHighQC(qc);
            
            // Create a new proposal extending from highQC
            HotStuffNode::Node_t* newNode = new HotStuffNode::Node_t();
            newNode->parent_hash = highQC->hash;
            newNode->height = highQC->height + 1;
            newNode->justify = *qc;
            
            // Generate hash for new node
            newNode->hash = std::to_string(currentView) + "_" + newNode->parent_hash;
            
            // Store the node
            nodes[newNode->hash] = newNode;
            
            // Broadcast prepare message
            currentPhase = 0; // Reset to prepare phase
            BroadcastForCurrentPhase(newNode);
            
            // Log phase change
            NS_LOG_INFO("Leader started new view with prepare message");
        }
    }
}

void 
HotStuffNode::OnReceiveVote(std::string vote, HotStuffNode::Node_t* node)
{
    // Log phase timing
    Time phaseTime = Simulator::Now() - phaseStartTime;
    NS_LOG_INFO("Node " << m_id << " commit phase took " << phaseTime.GetMilliSeconds() << "s");
    phaseStartTime = Simulator::Now();
    
    if (is_leader) {
        // Add vote to the collection
        if (nodes.find(node->hash) == nodes.end()) {
            nodes[node->hash] = node;
        }
        
        // Count votes for this node
        Node_t* votedNode = nodes[node->hash];
        votedNode->justify.signatures.push_back(vote);
        
        // Calculate quorum size
        int quorumSize = (2 * n_replicas / 3 + 1);
        
        // Check if we have enough votes for next phase
        if (votedNode->justify.signatures.size() >= quorumSize) {
            // Increment phase
            currentPhase = (currentPhase + 1) % 3;
            
            if (currentPhase == 0) {
                // Completed a full consensus cycle
                // Execute the command and finalize
                ExecuteCommand(votedNode->command);
                
                // Broadcast decide message
                std::string msg = std::to_string(DECIDE) + serializeNode(votedNode);
                Send((uint8_t*)msg.c_str(), msg.length());
                
                // Log consensus completion
                NS_LOG_INFO("Leader completed consensus cycle for view " << currentView);
                
                // Move to next view
                currentView++;
                consensusReached = true;
            } else {
                // Broadcast next phase message
                BroadcastForCurrentPhase(votedNode);
            }
        }
    }
}

void 
HotStuffNode::OnReceivePreCommit(Node_t* node)
{
    // Log phase timing
    Time phaseTime = Simulator::Now() - phaseStartTime;
    NS_LOG_INFO("Node " << m_id << " precommit phase took " << phaseTime.GetSeconds() << "s");
    phaseStartTime = Simulator::Now();
    
    if (SafeNode(node, &node->justify)) {
        // Store the node
        nodes[node->hash] = node;
        
        // Update the locked QC if this node extends from our prepare phase
        if (node->justify.view > (lockedQC ? lockedQC->justify.view : -1)) {
            lockedQC = node;
        }

        // Create and send vote for the pre-commit phase
        std::string vote = CreateVote(node);
        
        // Add to pending votes for batching
        pendingVotes.push_back(vote);
        
        // Send immediately if batch size reached or we're in a small network
        if (pendingVotes.size() >= BATCH_SIZE || n_replicas < BATCH_SIZE) {
            SendBatchedVotes();
        }

        // Log message for benchmarking
        std::string msg_id = "precommit_" + node->hash;
        LogMessageReceived(msg_id);
    } else {
        NS_LOG_WARN("Node " << m_id << " rejected unsafe precommit");
    }
}

} // namespace ns3