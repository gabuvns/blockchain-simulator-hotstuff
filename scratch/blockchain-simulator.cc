#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include "ns3/hotstuff-node.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("BlockchainSimulator");

// Collect performance metrics from all nodes and save to a file
void CollectMetrics(ApplicationContainer& nodeApps, int nodeCount, int txSize, double networkDelay) {
  double totalLatency = 0;
  uint32_t totalMessagesSent = 0;
  uint32_t totalMessagesReceived = 0;
  uint32_t nodesReachedConsensus = 0;
  
  std::ofstream outFile("blockchain_metrics_" + std::to_string(nodeCount) + 
                        "_nodes_" + std::to_string(txSize) + "_txsize_" + 
                        std::to_string(int(networkDelay*1000)) + "_ms.csv");
  
  outFile << "NodeID,MessagesSent,MessagesReceived,AvgLatency,ConsensusReached\n";
  
  for (uint32_t i = 0; i < nodeApps.GetN(); i++) {
    Ptr<HotStuffNode> node = DynamicCast<HotStuffNode>(nodeApps.Get(i));
    if (!node) {
      NS_LOG_WARN("Node " << i << " is not a HotStuffNode");
      continue;
    }
    
    totalMessagesSent += node->GetMessageCount();
    totalMessagesReceived += node->messages_received;
    totalLatency += node->GetAverageLatency();
    if (node->consensusReached) {
      nodesReachedConsensus++;
    }
    
    outFile << i << "," 
            << node->GetMessageCount() << "," 
            << node->messages_received << "," 
            << std::fixed << std::setprecision(6) << node->GetAverageLatency() << "," 
            << (node->consensusReached ? "1" : "0") << "\n";
  }
  
  outFile.close();
  
  std::cout << "=== Performance Metrics for " << nodeCount << " nodes ===" << std::endl;
  std::cout << "Nodes reaching consensus: " << nodesReachedConsensus << "/" << nodeCount 
            << " (" << (nodesReachedConsensus * 100.0 / nodeCount) << "%)" << std::endl;
  std::cout << "Average message latency: " << std::fixed << std::setprecision(6) 
            << (totalLatency / nodeApps.GetN()) << "s" << std::endl;
  std::cout << "Total messages sent: " << totalMessagesSent << std::endl;
  std::cout << "Total messages received: " << totalMessagesReceived << std::endl;
  std::cout << "Messages per node: " << (totalMessagesSent / nodeCount) << std::endl;
  std::cout << "=====================================" << std::endl;
}

// Create optimized network topology for consensus
// void startSimulator(int N)
// {
//   // Create nodes
//   NodeContainer nodes;
//   nodes.Create(N);

//   NetworkHelper networkHelper(N);
//   NetDeviceContainer devices;
//   PointToPointHelper pointToPoint;

//   // Configure network settings - maintaining the same parameters
//   pointToPoint.SetDeviceAttribute("DataRate", StringValue("3Mbps"));
//   pointToPoint.SetChannelAttribute("Delay", StringValue("3ms"));

//   // Install internet stack
//   InternetStackHelper stack;
//   stack.Install(nodes);

//   Ipv4AddressHelper address;
//   address.SetBase("1.0.0.0", "255.255.255.0");

//   // Use a star topology instead of a full mesh network
//   // Node 0 will be the center of the star and also the leader
//   for (int i = 1; i < N; i++) {
//     Ipv4InterfaceContainer interface;
//     Ptr<Node> centerNode = nodes.Get(0);
//     Ptr<Node> leafNode = nodes.Get(i);
    
//     NetDeviceContainer device = pointToPoint.Install(centerNode, leafNode);
    
//     interface.Add(address.Assign(device.Get(0)));
//     interface.Add(address.Assign(device.Get(1)));

//     // Set up node connections
//     networkHelper.m_nodesConnectionsIps[0].push_back(interface.GetAddress(1));
//     networkHelper.m_nodesConnectionsIps[i].push_back(interface.GetAddress(0));

//     // Move to next network
//     address.NewNetwork();
//   }

//   // Additionally connect some nodes directly to improve message propagation
//   // Add a few random connections between non-leader nodes if network is large enough
//   if (N > 5) {
//     int additionalLinks = std::min(N/2, 10); // Add up to 10 additional links
    
//     for (int i = 0; i < additionalLinks; i++) {
//       // Connect random nodes (excluding the leader)
//       int node1 = 1 + (i % (N-1));
//       int node2 = 1 + ((i + N/2) % (N-1));
      
//       if (node1 != node2) {
//         Ipv4InterfaceContainer interface;
//         Ptr<Node> p1 = nodes.Get(node1);
//         Ptr<Node> p2 = nodes.Get(node2);
        
//         NetDeviceContainer device = pointToPoint.Install(p1, p2);
        
//         interface.Add(address.Assign(device.Get(0)));
//         interface.Add(address.Assign(device.Get(1)));

//         networkHelper.m_nodesConnectionsIps[node1].push_back(interface.GetAddress(1));
//         networkHelper.m_nodesConnectionsIps[node2].push_back(interface.GetAddress(0));

//         address.NewNetwork();
//       }
//     }
//   }

//   // Install applications on nodes
//   ApplicationContainer nodeApp = networkHelper.Install(nodes);

//   // Set up simulation time
//   nodeApp.Start(Seconds(0.0));
//   nodeApp.Stop(Seconds(30.0)); // Increased from 10s to 30s to ensure consensus completes

//   // Run the simulation
//   Simulator::Run();
//   Simulator::Destroy();
// }

void startSimulator (int N, int txSize, double networkDelay, int txSpeed)
{
  // Set the HotStuffNode static variables before creating nodes
  HotStuffNode::tx_size = txSize;
  HotStuffNode::network_delay = networkDelay;
  
  NodeContainer nodes;
  nodes.Create (N);

  NetworkHelper networkHelper (N);
  // 默认pointToPint只能连接两个节点，需要手动连接
  NetDeviceContainer devices;
  PointToPointHelper pointToPoint;

  // 节点总带宽24Mbps，分到每一个点对点通道上为3Mbps
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("3Mbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("3ms"));
  uint32_t nNodes = nodes.GetN ();

  InternetStackHelper stack;
  stack.Install (nodes);

  Ipv4AddressHelper address;
  address.SetBase ("1.0.0.0", "255.255.255.0");

  // 网络节点两两建立连接
  for (int i = 0; i < N; i++) {
      for (int j = 0; j < N && j != i; j++) {
          Ipv4InterfaceContainer interface;
          Ptr<Node> p1 = nodes.Get (i);
          Ptr<Node> p2 = nodes.Get (j);
          NetDeviceContainer device = pointToPoint.Install(p1, p2);
          
          interface.Add(address.Assign (device.Get(0)));
          interface.Add(address.Assign (device.Get(1)));

          networkHelper.m_nodesConnectionsIps[i].push_back(interface.GetAddress(1));
          networkHelper.m_nodesConnectionsIps[j].push_back(interface.GetAddress(0));

          // 创建新的网络: 如果不增加网络的话, 所有ip都在一个字网，而最后一块device会覆盖之前的设置，导致无法通过ip访问到之前的邻居节点
          // 应该的设置：每个device连接的两个节点在一个字网内，所以每分配一次ip，地址应该增加一个网段
          address.NewNetwork();
      }
  }
  ApplicationContainer nodeApp = networkHelper.Install (nodes);

  nodeApp.Start (Seconds (0.0));
  nodeApp.Stop (Seconds (30.0));

  Simulator::Run ();
  
  // Collect metrics before destroying the simulator
  CollectMetrics(nodeApp, N, txSize, networkDelay);
  
  Simulator::Destroy ();
}



int
main(int argc, char *argv[])
{
  int txSize = 256; // Default transaction size in bytes
  double networkDelay = 0.1; // Default network delay in seconds (1ms)
  int txSpeed = 10000; // Default transaction speed (TPS)
  bool runNodeSeries = true; // Whether to run the series of node counts
  int nodeCount = 8; // Default node count
  bool enableLogging = false; // Whether to enable detailed logging
  
  CommandLine cmd;
  cmd.AddValue("txsize", "Transaction size in bytes", txSize);
  cmd.AddValue("delay", "Network delay in seconds", networkDelay);
  cmd.AddValue("txspeed", "Transaction speed (TPS)", txSpeed);
  cmd.AddValue("nodes", "Number of nodes in the network", nodeCount);
  cmd.AddValue("series", "Run simulations with 8, 32, 64 and 128 nodes", runNodeSeries);
  cmd.AddValue("log", "Enable detailed logging", enableLogging);
  cmd.Parse(argc, argv);
  
  Time::SetResolution(Time::NS);

  // Enable detailed logging if requested
  if (enableLogging) {
    LogComponentEnable("HotStuffNode", LOG_LEVEL_INFO);
    LogComponentEnable("BlockchainSimulator", LOG_LEVEL_INFO);
  }
  
  if (runNodeSeries) {
    // Run the original series of simulations with different node counts
    std::cout << "Running simulations with varying node counts..." << std::endl;
    
    NS_LOG_INFO("Starting simulation with 8 nodes");
    auto startTime = std::chrono::high_resolution_clock::now();
    startSimulator(8, txSize, networkDelay, txSpeed);
    auto endTime = std::chrono::high_resolution_clock::now();
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    std::cout << "Total Time 8 nodes: " << milliseconds.count() << "ms" << std::endl;
    
    NS_LOG_INFO("Starting simulation with 32 nodes");
    startTime = std::chrono::high_resolution_clock::now();
    startSimulator(32, txSize, networkDelay, txSpeed);
    endTime = std::chrono::high_resolution_clock::now();
    milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    std::cout << "Total Time 32 nodes: " << milliseconds.count() << "ms" << std::endl;
    
    NS_LOG_INFO("Starting simulation with 64 nodes");
    startTime = std::chrono::high_resolution_clock::now();
    startSimulator(64, txSize, networkDelay, txSpeed);
    endTime = std::chrono::high_resolution_clock::now();
    milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    std::cout << "Total Time 64 nodes: " << milliseconds.count() << "ms" << std::endl;
    
    NS_LOG_INFO("Starting simulation with 128 nodes");
    startTime = std::chrono::high_resolution_clock::now();
    startSimulator(128, txSize, networkDelay, txSpeed);
    endTime = std::chrono::high_resolution_clock::now();
    milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    std::cout << "Total Time 128 nodes: " << milliseconds.count() << "ms" << std::endl;
  } else {
    // Run a single simulation with the specified parameters
    std::cout << "Running simulation with " << nodeCount << " nodes, "
              << txSize << " byte transactions, "
              << networkDelay * 1000 << "ms network delay, and "
              << txSpeed << " TPS" << std::endl;
              
    auto startTime = std::chrono::high_resolution_clock::now();
    startSimulator(nodeCount, txSize, networkDelay, txSpeed);
    auto endTime = std::chrono::high_resolution_clock::now();
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    std::cout << "Total simulation time: " << milliseconds.count() << "ms" << std::endl;
  }
  
  return 0;
}