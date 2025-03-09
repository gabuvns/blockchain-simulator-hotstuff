#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include <chrono>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("BlockchainSimulator");

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

void startSimulator (int N)
{
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
  Simulator::Destroy ();
}



int
main(int argc, char *argv[])
{
  CommandLine cmd;
  cmd.Parse(argc, argv);
  
  Time::SetResolution(Time::NS);

  // Enable detailed logging
  // LogComponentEnable("HotStuffNode", LOG_LEVEL_INFO);
  // LogComponentEnable("BlockchainSimulator", LOG_LEVEL_INFO);
  
  NS_LOG_INFO("Starting simulation with 8 nodes");
  auto startTime = std::chrono::high_resolution_clock::now();
  startSimulator(8);
  auto endTime = std::chrono::high_resolution_clock::now();
  auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
  std::cout << "Total Time 8 nodes: " << milliseconds.count() << "ms" << std::endl;
  
  NS_LOG_INFO("Starting simulation with 32 nodes");
  startTime = std::chrono::high_resolution_clock::now();
  startSimulator(32);
  endTime = std::chrono::high_resolution_clock::now();
  milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
  std::cout << "Total Time 32 nodes: " << milliseconds.count() << "ms" << std::endl;
  
  NS_LOG_INFO("Starting simulation with 64 nodes");
  startTime = std::chrono::high_resolution_clock::now();
  startSimulator(64);
  endTime = std::chrono::high_resolution_clock::now();
  milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
  std::cout << "Total Time 64 nodes: " << milliseconds.count() << "ms" << std::endl;
  

  NS_LOG_INFO("Starting simulation with 128 nodes");
  startTime = std::chrono::high_resolution_clock::now();
  startSimulator(128);
  endTime = std::chrono::high_resolution_clock::now();
  milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
  std::cout << "Total Time 128 nodes: " << milliseconds.count() << "ms" << std::endl;
  
  return 0;
}