#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include <chrono>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("BlockchainSimulator");

// 创建网络
void startSimulator (int N)
{
  std::cout << "BEGININGa" << std::endl;

  NodeContainer nodes;
  std::cout << "BEGININGb" << std::endl;

  nodes.Create (N);
  std::cout << "BEGININGc" << std::endl;

  NetworkHelper networkHelper (N);
  // 默认pointToPint只能连接两个节点，需要手动连接
  NetDeviceContainer devices;
  PointToPointHelper pointToPoint;

  // 节点总带宽24Mbps，分到每一个点对点通道上为3Mbps
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("3Mbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("3ms"));
  // uint32_t nNodes = nodes.GetN ();

  InternetStackHelper stack;
  stack.Install (nodes);
  std::cout << "BEGINING3" << std::endl;

  Ipv4AddressHelper address;
  address.SetBase ("1.0.0.0", "255.255.255.0");
  std::cout << "BEGINING4" << std::endl;

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
  std::cout << "BEGINING5" << std::endl;

  ApplicationContainer nodeApp = networkHelper.Install (nodes);

  nodeApp.Start (Seconds (0.0));
  nodeApp.Stop (Seconds (10.0));

  Simulator::Run ();
  Simulator::Destroy ();
}


int
main (int argc, char *argv[])
{
  std::cout << "BEGINING1" << std::endl;

  CommandLine cmd;
  cmd.Parse (argc, argv);
  
  // numbers of nodes in network
  //int N = 5;
  
  Time::SetResolution (Time::NS);

  // 1.need changed to a specific protocol class
  LogComponentEnable ("HotStuffNode", LOG_LEVEL_INFO);
  // ============== 10 nos
  std::cout << "BEGINING2" << std::endl;

  auto startTime = std::chrono::high_resolution_clock::now();

  // startSimulator(std::stoi(argv[1]));
  startSimulator(8);
  auto endTime = std::chrono::high_resolution_clock::now();
  auto secondsTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

  std::cout << "Total Time 8 nos: " << secondsTime << std::endl;
  //========================= 32 nos 
   startTime = std::chrono::high_resolution_clock::now();
  startSimulator(32);
   endTime = std::chrono::high_resolution_clock::now();
   secondsTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

  std::cout << "Total Time 32 nos: " << secondsTime << std::endl;

  // ============================= 64 nos
   startTime = std::chrono::high_resolution_clock::now();
  startSimulator(64);
   endTime = std::chrono::high_resolution_clock::now();
   secondsTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

  std::cout << "Total Time 64 nos: " << secondsTime << std::endl;


  // ============================= 100 nos
     startTime = std::chrono::high_resolution_clock::now();
  startSimulator(128);
   endTime = std::chrono::high_resolution_clock::now();
   secondsTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

  std::cout << "Total Time 128 nos: " << secondsTime << std::endl;
  return 0;
}