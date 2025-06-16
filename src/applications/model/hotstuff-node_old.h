// #ifndef HOTSTUFF_NODE_H
// #define HOTSTUFF_NODE_H

// #include <algorithm>
// #include "ns3/application.h"
// #include "ns3/event-id.h"
// #include "ns3/ptr.h"
// #include "ns3/traced-callback.h"
// #include "ns3/address.h"
// #include "ns3/boolean.h"
// #include <map>

// namespace ns3 {

// class Address;
// class Socket;
// class Packet;

// class HotStuffNode : public Application 
// {
// public:
//     static TypeId GetTypeId (void);

//     void SetPeersAddresses (const std::vector<Ipv4Address> &peers);

//     HotStuffNode (void);
//     virtual ~HotStuffNode (void);
//     int N;
//     uint32_t        m_id;                               // node id
//     Ptr<Socket>     m_socket;                           // listening socket
//     std::map<Ipv4Address, Ptr<Socket>> m_peersSockets;  // peer sockets map
//     std::map<Address, std::string> m_bufferedData;      // buffered data map
//     Address         m_local;                            // local address
//     std::vector<Ipv4Address> m_peersAddresses;          // peer addresses

//     // HotStuff specific state variables
//     // Define internal types
//     typedef struct {
//         int view;
//         int height;
//         std::string node_hash;
//         std::vector<std::string> signatures;
//     } QC_t;

//     typedef struct {
//         std::string hash;
//         std::string parent_hash;
//         std::string command;
//         int height;
//         QC_t justify;
//     } Node_t;

//     // Protocol state
//     int currentView;                  // current view number
//     Node_t* highQC;                     // highest QC node
//     Node_t* lockedQC;                   // locked QC node
//     Node_t* committedQC;                // last committed QC node
//     std::map<std::string, Node_t*> nodes; // node storage
//     bool is_leader;                   // leader status
//     int n_replicas;                   // total number of replicas

//     // Benchmarking parameters
//     static int tx_size;               // Size of transaction in bytes
//     static double network_delay;      // Network delay in seconds
//     uint32_t messages_sent;           // Counter for sent messages
//     uint32_t messages_received;       // Counter for received messages
//     double total_latency;            // Sum of all message latencies
//     std::map<std::string, double> message_timestamps;  // Store send times for latency calculation
    
//     // Benchmarking methods
//     void LogMessageSent(const std::string& msg_id);
//     void LogMessageReceived(const std::string& msg_id);
//     double GetAverageLatency() const;
//     uint32_t GetMessageCount() const;

//     // Protocol methods
//     bool SafeNode(Node_t* node, QC_t* qc);
//     void UpdateHighQC(QC_t* qc);
//     void OnReceiveProposal(Node_t* node);
//     void OnReceiveVote(std::string vote, Node_t* node);
//     void OnReceiveNewView(QC_t* qc);
//     void BroadcastPrePrepare(Node_t* node);
//     void CreateNewQC(Node_t* node, std::vector<std::string> votes);
//     void OnReceivePreCommit(Node_t* node);  // Changed from QC_t* to Node_t*
//     // Helper methods for node and message handling
//     HotStuffNode::Node_t* deserializeNode(const std::string& data);
//     HotStuffNode::QC_t* deserializeQC(const std::string& data);
//     void ExecuteCommand(const std::string& command);
//     bool IsAncestor(HotStuffNode::Node_t* descendant, HotStuffNode::Node_t* ancestor);
//     std::string CreateVote(HotStuffNode::Node_t* node);
//     bool IsPreCommitQC(HotStuffNode::QC_t* qc);
//     std::string serializeNode(HotStuffNode::Node_t* node);
//     std::string serializeQC(HotStuffNode::QC_t* qc);

// protected:
//     virtual void StartApplication (void);    
//     virtual void StopApplication (void); 
//     void HandleRead (Ptr<Socket> socket);
//     std::string getPacketContent(Ptr<Packet> packet, Address from);
//     void Send (uint8_t* data, int size);
//     void Send (uint8_t* data, int size, Address from);
// };

// enum MessageType
// {
//     NEW_VIEW,       // 0
//     PREPARE,        // 1
//     PRECOMMIT,      // 2
//     COMMIT,         // 3
//     DECIDE         // 4
// };

// } // namespace ns3
// #endif