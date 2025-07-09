#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "vehicle.h"
#include "rsu.h"
#include "common.h"
#include "logging.h"
#include <vector>
#include <map>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <iomanip>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("RFTM");

void SetAttackerPercentage(NodeContainer &vehicles, double percentage, AttackType attackType)
{
    uint32_t totalVehicles = vehicles.GetN();
    uint32_t badVehiclesNeeded = static_cast<uint32_t>(totalVehicles * percentage / 100.0);
    NS_LOG_INFO("ATTACKER_PERCENTAGE_UPDATE: Setting " << badVehiclesNeeded << " vehicles as attackers (" << attackType << ") out of " << totalVehicles << " (" << percentage << "%)");
    for (uint32_t i = 0; i < badVehiclesNeeded; i++) 
    {
        Ptr<Vehicle> app = vehicles.Get(i)->GetApplication(0)->GetObject<Vehicle>();
        if (app->GetAttackType() != attackType) 
        {
            double delay = 1 + (i * 0.1); // Staggered delays
            Simulator::Schedule(Seconds(delay), &Vehicle::SetAttackType, app, attackType);            
        }
    }
}

void ScheduleDiscretePercentageUpdates(NodeContainer &vehicles, double simTime, const std::vector<double> &percentages, AttackType attackType)
{
    if (percentages.empty()) 
    {
        // NS_LOG_ERROR("No percentages provided for bad mouthing updates");
        return;
    }
    
    double intervalPerPercentage = simTime / percentages.size();    
    for (size_t i = 0; i < percentages.size(); ++i) 
    {
        double updateTime = i * intervalPerPercentage;
        double percentage = percentages[i];        
        Simulator::Schedule(Seconds(updateTime), &SetAttackerPercentage, std::ref(vehicles), percentage, attackType);
        // NS_LOG_INFO("Scheduled attacker percentage update at time " << updateTime << " with percentage " << percentage << "%");
    }
}

enum SimulationCase
{
    ONOFF_WITH_POSITIVE_VSP = 1,
    ONOFF_WITH_NEGATIVE_VSP = 2,
    BAD_MOUTHING_AGAINST_POSITIVE_VSP = 3,
    BALLOT_STUFFING_AGAINST_NEGATIVE_VSP = 4,
    
    /*
    // A1, A1'
    ONOFF_NEG_ON30_OFF70 = 5, -> ONOFF_OFF30_ON70 (14 or A5')
    ONOFF_NEG_OFF70_ON30 = 6, -> ONOFF_ON70_OFF30 (13 or A5)
    
    // A2, A2'
    ONOFF_NEG_ON40_OFF60 = 7, -> ONOFF_OFF40_ON60 (12 or A4')
    ONOFF_NEG_OFF60_ON40 = 8, -> ONOFF_ON60_OFF40 (11 or A4)
    
    // A3, A3'
    ONOFF_NEG_ON50_OFF50 = 9, -> ONOFF_OFF50_ON50 (10 or A3')
    ONOFF_NEG_OFF50_ON50 = 10, -> ONOFF_ON50_OFF50 (9 or A3)
    
    // A4, A4'
    ONOFF_NEG_ON60_OFF40 = 11, -> ONOFF_OFF60_ON40 (8 or A2')
    ONOFF_NEG_OFF40_ON60 = 12, -> ONOFF_ON40_OFF60 (7 or A2)
    
    // A5, A5'
    ONOFF_NEG_ON70_OFF30 = 13, -> ONOFF_OFF70_ON30 (6 or A1')
    ONOFF_NEG_OFF30_ON70 = 14, -> ONOFF_ON30_OFF70 (5 or A1)
    */

    // A1, A1'
    ONOFF_NEG_ON30_OFF70 = 5,
    ONOFF_NEG_OFF70_ON30 = 6,
    
    // A2, A2'
    ONOFF_NEG_ON40_OFF60 = 7,
    ONOFF_NEG_OFF60_ON40 = 8,
    
    // A3, A3'
    ONOFF_NEG_ON50_OFF50 = 9,
    ONOFF_NEG_OFF50_ON50 = 10,
    
    // A4, A4'
    ONOFF_NEG_ON60_OFF40 = 11,
    ONOFF_NEG_OFF40_ON60 = 12,
    
    // A5, A5'
    ONOFF_NEG_ON70_OFF30 = 13,
    ONOFF_NEG_OFF30_ON70 = 14,

    // B1, B1'
    NEG_50_50_VSP_150_INTERVAL = 15,
    NEG_50_50_VSP_300_INTERVAL = 16,

    VARYING_ONOFF_20_PERCENT = 17,
    VARYING_ONOFF_40_PERCENT = 18,
    VARYING_ONOFF_60_PERCENT = 19,
    
    MIXED_ATTACK = 20,
    DOUBLE_ATTACK = 21,

    FCM_TIMING_50_VEHICLES = 22,
    FCM_TIMING_100_VEHICLES = 23,
    FCM_TIMING_150_VEHICLES = 24,
    FCM_TIMING_200_VEHICLES = 25
};

void LogVehiclePositions(NodeContainer &vehicles, NodeContainer &serviceProviders)
{
    NS_LOG_INFO("======= Vehicle Positions =======");
    for (uint32_t i = 0; i < vehicles.GetN(); i++)
    {
        Ptr<Node> node = vehicles.Get(i);
        Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
        if (mobility)
        {
            Vector pos = mobility->GetPosition();
            NS_LOG_INFO("V" << node->GetId() << " position: x=" << pos.x << ", y=" << pos.y << ", z=" << pos.z);
        }
    }
    for (uint32_t i = 0; i < serviceProviders.GetN(); i++)
    {
        Ptr<Node> node = serviceProviders.Get(i);
        Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
        if (mobility)
        {
            Vector pos = mobility->GetPosition();
            NS_LOG_INFO("Service Provider " << node->GetId() << " position: x=" << pos.x << ", y=" << pos.y << ", z=" << pos.z);
        }
    }
    NS_LOG_INFO("================================");
}

void LogDistancesToServiceProvider(NodeContainer &vehicles, NodeContainer &serviceProviders)
{
    if (serviceProviders.GetN() == 0)
    {
        NS_LOG_INFO("No service providers to calculate distances to");
        return;
    }
    
    Ptr<Node> sp = serviceProviders.Get(0);
    Ptr<MobilityModel> spMobility = sp->GetObject<MobilityModel>();
    
    if (!spMobility)
    {
        NS_LOG_INFO("Service provider has no mobility model");
        return;
    }
    
    NS_LOG_INFO("======= Distances to Service Provider " << sp->GetId() << " =======");
    for (uint32_t i = 0; i < vehicles.GetN(); i++)
    {
        Ptr<Node> vehicle = vehicles.Get(i);
        Ptr<MobilityModel> vehicleMobility = vehicle->GetObject<MobilityModel>();
        
        if (vehicleMobility)
        {
            // Calculate distance between vehicle and service provider
            double distance = vehicleMobility->GetDistanceFrom(spMobility);
            NS_LOG_INFO("Distance from Vehicle " << vehicle->GetId() << " to VSP " << sp->GetId() << ": " << distance << " meters");
        }
    }
    NS_LOG_INFO("================================================");
}

void LogDistancesToRSU(NodeContainer &vehicles, NodeContainer &serviceProviders, NodeContainer &rsuNodes)
{
    if (rsuNodes.GetN() == 0)
    {
        NS_LOG_INFO("No RSU node to calculate distances to");
        return;
    }
    
    Ptr<Node> rsu = rsuNodes.Get(0);
    Ptr<MobilityModel> rsuMobility = rsu->GetObject<MobilityModel>();
    
    if (!rsuMobility)
    {
        NS_LOG_INFO("RSU has no mobility model");
        return;
    }
    
    Vector rsuPos = rsuMobility->GetPosition();
    NS_LOG_INFO("======= Distances to RSU (Node " << rsu->GetId() << " at x=" << rsuPos.x << ", y=" << rsuPos.y << ", z=" << rsuPos.z << ") =======");
    for (uint32_t i = 0; i < vehicles.GetN(); i++)
    {
        Ptr<Node> vehicle = vehicles.Get(i);
        Ptr<MobilityModel> vehicleMobility = vehicle->GetObject<MobilityModel>();
        
        if (vehicleMobility)
        {
            // Calculate distance between vehicle and RSU
            double distance = vehicleMobility->GetDistanceFrom(rsuMobility);
            NS_LOG_INFO("Distance from V" << vehicle->GetId() << " to RSU: " << distance << " meters");
        }
    }
    for (uint32_t i = 0; i < serviceProviders.GetN(); i++)
    {
        Ptr<Node> sp = serviceProviders.Get(i);
        Ptr<MobilityModel> spMobility = sp->GetObject<MobilityModel>();
        
        if (spMobility)
        {
            // Calculate distance between VSP and RSU
            double distance = spMobility->GetDistanceFrom(rsuMobility);
            NS_LOG_INFO("Distance from VSP" << sp->GetId() << " to RSU: " << distance << " meters");
        }
    }
    NS_LOG_INFO("================================================");
}

void ConfigureOnOffForVSP(Ptr<Vehicle> vspApp, double onTime, double offTime, bool startWithOn)
{
    vspApp->SetOnTime(onTime);
    vspApp->SetOffTime(offTime);
    vspApp->SetStartWithOn(startWithOn);
    vspApp->SetIsInOnOffMode(true);
    // NS_LOG_INFO("Configured VSP" << vspApp->GetNode()->GetId() << " with OnTime=" << onTime << ", OffTime=" << offTime << ", StartWithOn=" << startWithOn);
}

void SetupCircularPositioning(NodeContainer &allVehicles, NodeContainer &allVSPs, NodeContainer &rsuNodes)
{
    uint32_t totalNodes = allVehicles.GetN() + allVSPs.GetN();
    // NS_LOG_INFO("Circular positioning for " << totalNodes << " nodes");
    
    // RSU at center (0, 0)
    MobilityHelper rsuMobility;
    rsuMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    rsuMobility.Install(rsuNodes);
    
    Ptr<MobilityModel> rsuMobilityModel = rsuNodes.Get(0)->GetObject<MobilityModel>();
    rsuMobilityModel->SetPosition(Vector(0.0, 0.0, 0.0));
    
    // Setup mobility
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(allVehicles);
    mobility.Install(allVSPs);
    
    std::vector<Ptr<Node>> allNodes;
    for (uint32_t i = 0; i < allVehicles.GetN(); i++) 
    {
        allNodes.push_back(allVehicles.Get(i));
    }
    for (uint32_t i = 0; i < allVSPs.GetN(); i++) 
    {
        allNodes.push_back(allVSPs.Get(i));
    }
    
    uint32_t nodeIndex = 0;
    // Inner circle: radius 10m
    uint32_t innerNodes = std::min(8u, totalNodes);
    for (uint32_t i = 0; i < innerNodes && nodeIndex < totalNodes; i++, nodeIndex++) 
    {
        double angle = 2.0 * M_PI * i / innerNodes;
        Vector pos = Vector(10.0 * cos(angle), 10.0 * sin(angle), 0.0);
        Ptr<MobilityModel> mobility = allNodes[nodeIndex]->GetObject<MobilityModel>();
        mobility->SetPosition(pos);
    }
    
    // Middle circle: radius 15m
    uint32_t middleNodes = std::min(16u, totalNodes - nodeIndex);
    for (uint32_t i = 0; i < middleNodes && nodeIndex < totalNodes; i++, nodeIndex++) 
    {
        double angle = 2.0 * M_PI * i / middleNodes;
        Vector pos = Vector(15.0 * cos(angle), 15.0 * sin(angle), 0.0);
        Ptr<MobilityModel> mobility = allNodes[nodeIndex]->GetObject<MobilityModel>();
        mobility->SetPosition(pos);
    }
    
    // Outer circle: radius 20m
    uint32_t outerNodes = totalNodes - nodeIndex;
    for (uint32_t i = 0; i < outerNodes && nodeIndex < totalNodes; i++, nodeIndex++) 
    {
        double angle = 2.0 * M_PI * i / outerNodes;
        Vector pos = Vector(20.0 * cos(angle), 20.0 * sin(angle), 0.0);
        Ptr<MobilityModel> mobility = allNodes[nodeIndex]->GetObject<MobilityModel>();
        mobility->SetPosition(pos);
    }
    // NS_LOG_INFO("Positioned: Inner(" << innerNodes << ") + Middle(" << middleNodes << ") + Outer(" << outerNodes << ") = " << totalNodes << " nodes");
}

void SetupAdaptivePositioning(NodeContainer &allVehicles, NodeContainer &allVSPs, NodeContainer &rsuNodes, double maxDistance)
{
    uint32_t totalNodes = allVehicles.GetN() + allVSPs.GetN();
    
    // Calculate grid parameters
    uint32_t gridSize = static_cast<uint32_t>(ceil(sqrt(totalNodes * 1.3)));    // 30% extra space
    if (gridSize < 12) gridSize = 12;                                           // Minimum for reasonable distribution
    
    uint32_t centerPos = gridSize / 2;
    double maxCornerDistance = sqrt(2.0 * centerPos * centerPos);
    double spacing = (maxDistance * 0.88) / maxCornerDistance;                  // 88% of max for safety
    
    Vector rsuPosition = Vector(centerPos * spacing, centerPos * spacing, 0.0);
    
    // NS_LOG_INFO("Adaptive grid: " << gridSize << "x" << gridSize << ", spacing=" << spacing << "m, max_dist≈" << (spacing * maxCornerDistance) << "m");
    
    // Setup RSU
    MobilityHelper rsuMobility;
    rsuMobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    rsuMobility.Install(rsuNodes);
    
    Ptr<MobilityModel> rsuMobilityModel = rsuNodes.Get(0)->GetObject<MobilityModel>();
    rsuMobilityModel->SetPosition(rsuPosition);
    
    // Setup grid for vehicles and VSPs
    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                  "MinX", DoubleValue(0.0),
                                  "MinY", DoubleValue(0.0),
                                  "DeltaX", DoubleValue(spacing),
                                  "DeltaY", DoubleValue(spacing),
                                  "GridWidth", UintegerValue(gridSize),
                                  "LayoutType", StringValue("RowFirst"));
    
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(allVehicles);
    mobility.Install(allVSPs);
}

void RunSimulationCase(SimulationCase caseType)
{
    auto startTime = std::chrono::high_resolution_clock::now();
    NS_LOG_INFO("======= SIMULATION START: Case " << caseType << " =======");
    
    ns3::EnableTimeLogging();

    // Simulation parameters
    uint32_t nHonestVehicles = 0;
    uint32_t nBadVehicles = 0;
    uint32_t nPositiveVSP = 0;
    uint32_t nNegativeVSP = 0;
    double simTime = SIMULATION_TIME;
    uint32_t nRsu = 1;
    double rsuExpectedTrustValue = 1.0;
    AttackType attackType = NO_ATTACK;
    
    // OnOff parameters
    bool enableOnOff = false;
    double onTime = DEFAULT_ON_PERIOD;
    double offTime = DEFAULT_OFF_PERIOD;
    bool startWithOn = true;

    // 50/50 VSP parameters
    double customServiceInterval = SERVICE_INTERVAL;
    double vehicleServiceProbability = 0.95;
    double positiveVspServiceProbability = 0.95;
    double negativeVspServiceProbability = positiveVspServiceProbability;

    // Set simulation parameters based on case type
    switch (caseType)
    {
    case ONOFF_WITH_POSITIVE_VSP:
        nHonestVehicles = 105;  // 70%
        nBadVehicles = 45;      // 30%
        nPositiveVSP = 5;
        nNegativeVSP = 0;
        attackType = NO_ATTACK;
        rsuExpectedTrustValue = 1.0; 
        break;

    case ONOFF_WITH_NEGATIVE_VSP:
        nHonestVehicles = 105;      // 70%
        nBadVehicles = 45;          // 30%
        nPositiveVSP = 0;
        nNegativeVSP = 5;
        attackType = NO_ATTACK;
        rsuExpectedTrustValue = 0.0;
        break;

    case BAD_MOUTHING_AGAINST_POSITIVE_VSP:
        nHonestVehicles = 150;      // 100% initially, will be dynamically updated
        nBadVehicles = 0;           // 0% initially, will be dynamically updated
        nPositiveVSP = 5;
        nNegativeVSP = 0;
        attackType = BAD_MOUTHING;
        rsuExpectedTrustValue = 1.0; 
        break;

    case BALLOT_STUFFING_AGAINST_NEGATIVE_VSP:
        nHonestVehicles = 150;  
        nBadVehicles = 0;       
        nPositiveVSP = 0;
        nNegativeVSP = 5;
        attackType = BALLOT_STUFFING;
        rsuExpectedTrustValue = 0.0;
        break;
        
    // A1: On 30%, Off 70% - starting with ON
    case ONOFF_NEG_ON30_OFF70:
        nHonestVehicles = 50;
        nBadVehicles = 0;
        nPositiveVSP = 0;
        nNegativeVSP = 1;
        attackType = ON_OFF;
        enableOnOff = true;
        onTime = 30.0;
        offTime = 70.0;
        startWithOn = true;
        rsuExpectedTrustValue = 0.0;
        break;
        
    // A1': Off 70%, On 30% - starting with OFF
    case ONOFF_NEG_OFF70_ON30:
        nHonestVehicles = 50;
        nBadVehicles = 0;
        nPositiveVSP = 0;
        nNegativeVSP = 1;
        attackType = ON_OFF;
        enableOnOff = true;
        onTime = 30.0;
        offTime = 70.0;
        startWithOn = false;
        rsuExpectedTrustValue = 0.0;
        break;
        
    // A2: On 40%, Off 60% - starting with ON
    case ONOFF_NEG_ON40_OFF60:
        nHonestVehicles = 50;
        nBadVehicles = 0;
        nPositiveVSP = 0;
        nNegativeVSP = 1;
        attackType = ON_OFF;
        enableOnOff = true;
        onTime = 40.0;
        offTime = 60.0;
        startWithOn = true;
        rsuExpectedTrustValue = 0.0;
        break;
        
    // A2': Off 60%, On 40% - starting with OFF
    case ONOFF_NEG_OFF60_ON40:
        nHonestVehicles = 50;
        nBadVehicles = 0;
        nPositiveVSP = 0;
        nNegativeVSP = 1;
        attackType = ON_OFF;
        enableOnOff = true;
        onTime = 40.0;
        offTime = 60.0;
        startWithOn = false;
        rsuExpectedTrustValue = 0.0;
        break;
        
    // A3: On 50%, Off 50% - starting with ON
    case ONOFF_NEG_ON50_OFF50:
        nHonestVehicles = 50;
        nBadVehicles = 0;
        nPositiveVSP = 0;
        nNegativeVSP = 1;
        attackType = ON_OFF;
        enableOnOff = true;
        onTime = 50.0;
        offTime = 50.0;
        startWithOn = true;
        rsuExpectedTrustValue = 0.0;
        break;
        
    // A3': Off 50%, On 50% - starting with OFF
    case ONOFF_NEG_OFF50_ON50:
        nHonestVehicles = 50;
        nBadVehicles = 0;
        nPositiveVSP = 0;
        nNegativeVSP = 1;
        attackType = ON_OFF;
        enableOnOff = true;
        onTime = 50.0;
        offTime = 50.0;
        startWithOn = false;
        rsuExpectedTrustValue = 0.0;
        break;
        
    // A4: On 60%, Off 40% - starting with ON
    case ONOFF_NEG_ON60_OFF40:
        nHonestVehicles = 50;
        nBadVehicles = 0;
        nPositiveVSP = 0;
        nNegativeVSP = 1;
        attackType = ON_OFF;
        enableOnOff = true;
        onTime = 60.0;
        offTime = 40.0;
        startWithOn = true;
        rsuExpectedTrustValue = 0.0;
        break;
        
    // A4': Off 40%, On 60% - starting with OFF
    case ONOFF_NEG_OFF40_ON60:
        nHonestVehicles = 50;
        nBadVehicles = 0;
        nPositiveVSP = 0;
        nNegativeVSP = 1;
        attackType = ON_OFF;
        enableOnOff = true;
        onTime = 60.0;
        offTime = 40.0;
        startWithOn = false;
        rsuExpectedTrustValue = 0.0;
        break;
        
    // A5: On 70%, Off 30% - starting with ON
    case ONOFF_NEG_ON70_OFF30:
        nHonestVehicles = 50;
        nBadVehicles = 0;
        nPositiveVSP = 0;
        nNegativeVSP = 1;
        attackType = ON_OFF;
        enableOnOff = true;
        onTime = 70.0;
        offTime = 30.0;
        startWithOn = true;
        rsuExpectedTrustValue = 0.0;
        break;
        
    // A5': Off 30%, On 70% - starting with OFF
    case ONOFF_NEG_OFF30_ON70:
        nHonestVehicles = 50;
        nBadVehicles = 0;
        nPositiveVSP = 0;
        nNegativeVSP = 1;
        attackType = ON_OFF;
        enableOnOff = true;
        onTime = 70.0;
        offTime = 30.0;
        startWithOn = false;
        rsuExpectedTrustValue = 0.0;
        break;

    // B1: 50/50 feedback with 150s service interval - negative VSP
    case NEG_50_50_VSP_150_INTERVAL:
        nHonestVehicles = 50;
        nBadVehicles = 0;
        nPositiveVSP = 0;
        nNegativeVSP = 1;
        attackType = NO_ATTACK;
        customServiceInterval = 150.0;
        rsuExpectedTrustValue = 0.0;
        negativeVspServiceProbability = 0.5;
        break;

    // B2: 50/50 feedback with 300s service interval - negative VSP
    case NEG_50_50_VSP_300_INTERVAL:
        nHonestVehicles = 50;
        nBadVehicles = 0;
        nPositiveVSP = 0;
        nNegativeVSP = 1;
        attackType = NO_ATTACK;
        customServiceInterval = 300.0;
        rsuExpectedTrustValue = 0.0;
        negativeVspServiceProbability = 0.5;
        break;

    case VARYING_ONOFF_20_PERCENT:
        nHonestVehicles = 50;
        nBadVehicles = 0;
        nPositiveVSP = 4;
        nNegativeVSP = 1;
        attackType = NO_ATTACK;
        rsuExpectedTrustValue = 1;
        negativeVspServiceProbability = 0.5;
        break;

    case VARYING_ONOFF_40_PERCENT:
        nHonestVehicles = 50;
        nBadVehicles = 0;
        nPositiveVSP = 3;
        nNegativeVSP = 2;
        attackType = NO_ATTACK;
        rsuExpectedTrustValue = 1;
        negativeVspServiceProbability = 0.5;
        break;

    case VARYING_ONOFF_60_PERCENT:
        nHonestVehicles = 50;
        nBadVehicles = 0;
        nPositiveVSP = 2;
        nNegativeVSP = 3;
        attackType = NO_ATTACK;
        rsuExpectedTrustValue = 1;
        negativeVspServiceProbability = 0.5;
        break;

    case MIXED_ATTACK:
        nHonestVehicles = 40;
        nBadVehicles = 10;  // 5 for bad-mouthing, 5 for ballot-stuffing
        nPositiveVSP = 1;
        nNegativeVSP = 2;   // 1 negative, 1 on-off
        attackType = NO_ATTACK;  // Will set later
        break;

    case DOUBLE_ATTACK:
        nHonestVehicles = 40;
        nBadVehicles = 10;              // On-off bad-mouthing attackers
        nPositiveVSP = 1;
        nNegativeVSP = 2;               // 1 negative, 1 on-off
        attackType = NO_ATTACK;         // Will set dynamically
        break;

    case FCM_TIMING_50_VEHICLES:
        nHonestVehicles = 25;
        nBadVehicles = 25;
        nPositiveVSP = 1;
        nNegativeVSP = 0;
        attackType = NO_ATTACK;
        simTime = 525.0;
        break;

    case FCM_TIMING_100_VEHICLES:
        nHonestVehicles = 50;
        nBadVehicles = 50;
        nPositiveVSP = 1;
        nNegativeVSP = 0;
        attackType = NO_ATTACK;
        simTime = 525.0;
        break;

    case FCM_TIMING_150_VEHICLES:
        nHonestVehicles = 75;
        nBadVehicles = 75;
        nPositiveVSP = 1;
        nNegativeVSP = 0;
        attackType = NO_ATTACK;
        simTime = 525.0;
        break;

    case FCM_TIMING_200_VEHICLES:
        nHonestVehicles = 100;
        nBadVehicles = 100;
        nPositiveVSP = 1;
        nNegativeVSP = 0;
        attackType = NO_ATTACK;
        simTime = 525.0;
        break;

    default:
        NS_LOG_ERROR("Invalid simulation case type");
        return;
    }

    // NS_LOG_INFO("Starting Simulation Case " << caseType);
    // NS_LOG_INFO("Honest Vehicles: " << nHonestVehicles);
    // NS_LOG_INFO("Bad Vehicles: " << nBadVehicles);
    // NS_LOG_INFO("Positive VSPs: " << nPositiveVSP);
    // NS_LOG_INFO("Negative VSPs: " << nNegativeVSP);
    
    // if (enableOnOff) 
    // {
    //     NS_LOG_INFO("OnOff Enabled: ON time=" << onTime << ", OFF time=" << offTime);
    // }

    // Create nodes
    NodeContainer honestVehicles;
    honestVehicles.Create(nHonestVehicles);

    NodeContainer badVehicles;
    badVehicles.Create(nBadVehicles);

    NodeContainer allVehicles;
    allVehicles.Add(honestVehicles);
    allVehicles.Add(badVehicles);

    NodeContainer positiveVSPs;
    positiveVSPs.Create(nPositiveVSP);

    NodeContainer negativeVSPs;
    negativeVSPs.Create(nNegativeVSP);

    NodeContainer allVSPs;
    allVSPs.Add(positiveVSPs);
    allVSPs.Add(negativeVSPs);

    NodeContainer rsuNodes;
    rsuNodes.Create(nRsu);

    // Setup mobility & positioning
    uint32_t totalNodes = allVehicles.GetN() + allVSPs.GetN();
    if (totalNodes <= 50) 
    {
        SetupCircularPositioning(allVehicles, allVSPs, rsuNodes);
    }
    else 
    {
        SetupAdaptivePositioning(allVehicles, allVSPs, rsuNodes, 20.0);
    }

    // LogVehiclePositions(allVehicles, allVSPs);
    // LogDistancesToServiceProvider(allVehicles, allVSPs);
    // LogDistancesToRSU(allVehicles, allVSPs, rsuNodes);

    // Setup WiFi
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211p);

    YansWifiPhyHelper wifiPhy;
    YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default();
    wifiPhy.SetChannel(wifiChannel.Create());

    WifiMacHelper wifiMac;
    wifiMac.SetType("ns3::AdhocWifiMac");

    NetDeviceContainer allVehicleDevices = wifi.Install(wifiPhy, wifiMac, allVehicles);
    NetDeviceContainer allServiceProviderDevices = wifi.Install(wifiPhy, wifiMac, allVSPs);
    NetDeviceContainer rsuDevices = wifi.Install(wifiPhy, wifiMac, rsuNodes);

    // Install internet stack
    InternetStackHelper internet;
    internet.Install(allVehicles);
    internet.Install(allVSPs);
    internet.Install(rsuNodes);

    // Assign IP addresses
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer allVehicleInterfaces = ipv4.Assign(allVehicleDevices);
    Ipv4InterfaceContainer allServiceProviderInterfaces = ipv4.Assign(allServiceProviderDevices);
    Ipv4InterfaceContainer rsuInterfaces = ipv4.Assign(rsuDevices);

    // Create and install RSU applications
    Ptr<RSU> rsuApp = CreateObject<RSU>();
    rsuApp->SetStartTime(Seconds(0));
    rsuApp->SetStopTime(Seconds(simTime));
    rsuApp->SetExpectedTrustValue(rsuExpectedTrustValue);
    rsuNodes.Get(0)->AddApplication(rsuApp);
    // NS_LOG_INFO("Created RSU" << rsuNodes.Get(0)->GetId());

    if (caseType == VARYING_ONOFF_20_PERCENT || 
        caseType == VARYING_ONOFF_40_PERCENT || 
        caseType == VARYING_ONOFF_60_PERCENT) 
    {
        std::vector<uint32_t> trackedVspIds;
        // In these cases, all negative VSPs are the ones behaving like on-off attackers
        // (providing 50% good service, 50% bad)
        for (uint32_t i = 0; i < negativeVSPs.GetN(); i++) 
        {
            trackedVspIds.push_back(negativeVSPs.Get(i)->GetId());
        }
        rsuApp->SetTrackedVsps(trackedVspIds);
        NS_LOG_INFO("Set " << trackedVspIds.size() << " negative VSPs to be tracked");
    }
    else if (caseType == MIXED_ATTACK || caseType == DOUBLE_ATTACK)
    {
        std::vector<uint32_t> trackedVspIds;
        for (uint32_t i = 0; i < positiveVSPs.GetN(); i++) 
        {
            trackedVspIds.push_back(positiveVSPs.Get(i)->GetId());
        }
        for (uint32_t i = 0; i < negativeVSPs.GetN(); i++) 
        {
            trackedVspIds.push_back(negativeVSPs.Get(i)->GetId());
        }
        rsuApp->SetTrackedVsps(trackedVspIds);
    }

    // Create and install Honest Vehicle applications
    for (uint32_t i = 0; i < honestVehicles.GetN(); i++)
    {
        Ptr<Vehicle> app = CreateObject<Vehicle>();
        app->SetNodeType(ORDINARY);
        honestVehicles.Get(i)->AddApplication(app);
        app->SetBad(false);
        app->SetStartTime(Seconds(0));
        app->SetStopTime(Seconds(simTime));
        app->SetAttackType(NO_ATTACK);
        app->SetServiceInterval(customServiceInterval);
        app->SetVspNonVspInterval(customServiceInterval / 2.0);
        app->SetServiceProbability(vehicleServiceProbability);
    }

    // Create and install Bad Vehicle applications
    for (uint32_t i = 0; i < badVehicles.GetN(); i++)
    {
        Ptr<Vehicle> app = CreateObject<Vehicle>();
        app->SetNodeType(ORDINARY);
        badVehicles.Get(i)->AddApplication(app);
        app->SetBad(true);
        app->SetStartTime(Seconds(0));
        app->SetStopTime(Seconds(simTime));
        app->SetAttackType(attackType);
        app->SetServiceInterval(customServiceInterval);
        app->SetVspNonVspInterval(customServiceInterval / 2.0);
        app->SetServiceProbability(vehicleServiceProbability);

        if (caseType == MIXED_ATTACK) 
        {
            if (i<5)
            {
                app->SetAttackType(BAD_MOUTHING);
                NS_LOG_INFO("Set V" << badVehicles.Get(i)->GetId() << " to BAD_MOUTHING");
            }
            else if (i<10)
            {
                app->SetAttackType(BALLOT_STUFFING);
                NS_LOG_INFO("Set V" << badVehicles.Get(i)->GetId() << " to BALLOT_STUFFING");
            }
        }
    }

    // Create and install Positive VSP applications
    for (uint32_t i = 0; i < positiveVSPs.GetN(); i++)
    {
        Ptr<Vehicle> app = CreateObject<Vehicle>();
        app->SetNodeType(SERVICE_PROVIDER);
        positiveVSPs.Get(i)->AddApplication(app);
        app->SetBad(false);
        app->SetStartTime(Seconds(0));
        app->SetStopTime(Seconds(simTime));
        app->SetServiceQuality(POSITIVE_SERVICE);
        app->SetServiceInterval(customServiceInterval);
        app->SetVspNonVspInterval(customServiceInterval / 2.0);
        app->SetServiceProbability(positiveVspServiceProbability);
        if (enableOnOff) 
        {
            ConfigureOnOffForVSP(app, onTime, offTime, startWithOn);
        }
    }

    // Create and install Negative VSP applications
    for (uint32_t i = 0; i < negativeVSPs.GetN(); i++)
    {
        Ptr<Vehicle> app = CreateObject<Vehicle>();
        app->SetNodeType(SERVICE_PROVIDER);
        negativeVSPs.Get(i)->AddApplication(app);
        app->SetBad(false); 
        app->SetStartTime(Seconds(0));
        app->SetStopTime(Seconds(simTime));
        app->SetServiceQuality(NEGATIVE_SERVICE);
        app->SetServiceInterval(customServiceInterval);
        app->SetVspNonVspInterval(customServiceInterval / 2.0);
        app->SetServiceProbability(negativeVspServiceProbability);
        if (enableOnOff) 
        {
            ConfigureOnOffForVSP(app, onTime, offTime, startWithOn);
        }

        if (caseType == MIXED_ATTACK) 
        {
            if (i == 0) 
            {
                app->SetServiceProbability(0.5);
                NS_LOG_INFO("Configured VSP" << negativeVSPs.Get(0)->GetId() << " with service probability 0.5");
            }
        }
    }
    
    if (caseType == BAD_MOUTHING_AGAINST_POSITIVE_VSP || caseType == BALLOT_STUFFING_AGAINST_NEGATIVE_VSP)
    {
        std::vector<double> attackerParcentages = {10.0, 20.0, 30.0, 40.0, 50.0, 60.0};        
        ScheduleDiscretePercentageUpdates(allVehicles, simTime, attackerParcentages, attackType);
    }

    if (caseType == DOUBLE_ATTACK) 
    {
        // Set up the on-off bad-mouthing vehicles
        for (uint32_t i = 0; i < badVehicles.GetN(); i++) 
        {
            Ptr<Vehicle> app = DynamicCast<Vehicle>(badVehicles.Get(i)->GetApplication(0));
            
            // Start with no attack
            app->SetAttackType(NO_ATTACK);
            
            // Schedule the first cycle to start after the first trust calculation (around 20 seconds)
            double startTime = 20.0;
            for (double cycleStart = startTime; cycleStart < simTime; cycleStart += 100.0) 
            {
                // Bad-mouthing for 25 seconds
                Simulator::Schedule(Seconds(cycleStart + 25.0), &Vehicle::SetAttackType, app, BAD_MOUTHING);
                // Honest for 25 seconds
                Simulator::Schedule(Seconds(cycleStart + 50.0), &Vehicle::SetAttackType, app, NO_ATTACK);
                // Bad-mouthing for 25 seconds
                Simulator::Schedule(Seconds(cycleStart + 75.0), &Vehicle::SetAttackType, app, BAD_MOUTHING);
                // Reset to honest at the end of the cycle
                Simulator::Schedule(Seconds(cycleStart + 100.0), &Vehicle::SetAttackType, app, NO_ATTACK);
            }
        }
        
        // Set up the on-off VSP (second negative VSP)
        if (negativeVSPs.GetN() >= 2) 
        {
            Ptr<Vehicle> onOffVspApp = DynamicCast<Vehicle>(negativeVSPs.Get(1)->GetApplication(0));
            // Start as a regular negative VSP with 95% negative service
            onOffVspApp->SetServiceQuality(NEGATIVE_SERVICE);
            onOffVspApp->SetServiceProbability(negativeVspServiceProbability);
            // Schedule the on-off behavior for each cycle
            double startTime = 20.0;
            for (double cycleStart = startTime; cycleStart < simTime; cycleStart += 100.0) 
            {
                // On-off for 75 seconds (50% probability)
                Simulator::Schedule(Seconds(cycleStart + 25.0), &Vehicle::SetServiceProbability, onOffVspApp, 0.5);
                // Reset to regular negative VSP at the end of the cycle
                Simulator::Schedule(Seconds(cycleStart + 100.0), &Vehicle::SetServiceProbability, onOffVspApp, negativeVspServiceProbability);
            }
        }
    }

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // For VARYING_ONOFF cases, log the final average trust values
    if (caseType == VARYING_ONOFF_20_PERCENT || 
        caseType == VARYING_ONOFF_40_PERCENT || 
        caseType == VARYING_ONOFF_60_PERCENT) 
    {
        Ptr<RSU> rsuApp = DynamicCast<RSU>(rsuNodes.Get(0)->GetApplication(0));
        auto allAvgGlobalTrustValues = rsuApp->GetAllAvgGlobalTrustValues();
        
        NS_LOG_INFO("Average Global Trust Values for Case " << caseType << ":");
        double overallAverageAcrossSPs = 0.0;
        int spCount = 0;
        
        // Only include the negative VSPs (on-off attackers) in the average
        for (uint32_t i = 0; i < negativeVSPs.GetN(); i++) 
        {
            uint32_t vspId = negativeVSPs.Get(i)->GetId();
            auto it = allAvgGlobalTrustValues.find(vspId);
            if (it != allAvgGlobalTrustValues.end()) 
            {
                // Calculate average for this SP from its history
                double spAverage = 0.0;
                const std::vector<double>& trustHistory = it->second;
                
                if (!trustHistory.empty()) 
                {
                    for (const double& trustValue : trustHistory) 
                    {
                        spAverage += trustValue;
                    }
                    spAverage /= trustHistory.size();
                    
                    std::stringstream historyStr;
                    historyStr << "VSP" << vspId << " AGT History: [";
                    for (size_t j = 0; j < trustHistory.size(); ++j) 
                    {
                        historyStr << trustHistory[j];
                        if (j < trustHistory.size() - 1) 
                        {
                            historyStr << ", ";
                        }
                    }
                    historyStr << "]";
                    NS_LOG_INFO(historyStr.str());
                    NS_LOG_INFO("  - VSP" << vspId << " Average: " << spAverage << " (from " << trustHistory.size() << " measurements)");
                    overallAverageAcrossSPs += spAverage;
                    spCount++;
                }
            }
        }
        
        if (spCount > 0) 
        {
            overallAverageAcrossSPs /= spCount;
            NS_LOG_INFO("Overall Average Global Trust for " << (caseType == VARYING_ONOFF_20_PERCENT ? "20%" : caseType == VARYING_ONOFF_40_PERCENT ? "40%" : "60%") << " on-off attackers: " << overallAverageAcrossSPs);
        }
    }

    else if (caseType == MIXED_ATTACK)
    {
        Ptr<RSU> rsuApp = DynamicCast<RSU>(rsuNodes.Get(0)->GetApplication(0));
        auto allAvgGlobalTrustValues = rsuApp->GetAllAvgGlobalTrustValues();
        NS_LOG_INFO("Average Global Trust Values History for Case " << caseType << ":");
        for (uint32_t i = 0; i < positiveVSPs.GetN(); i++) 
        {
            uint32_t vspId = positiveVSPs.Get(i)->GetId();
            auto it = allAvgGlobalTrustValues.find(vspId);
            if (it != allAvgGlobalTrustValues.end()) 
            {
                std::stringstream ss;
                ss << "Positive VSP" << vspId << " AGT History: [";
                for (size_t j = 0; j < it->second.size(); ++j) 
                {
                    ss << it->second[j];
                    if (j < it->second.size() - 1) 
                    {
                        ss << ", ";
                    }
                }
                ss << "]";
                NS_LOG_INFO(ss.str());
            }
        }
        
        for (uint32_t i = 0; i < negativeVSPs.GetN(); i++) 
        {
            uint32_t vspId = negativeVSPs.Get(i)->GetId();
            std::string vspType = (i == 0) ? "OnOff" : "Negative";
            auto it = allAvgGlobalTrustValues.find(vspId);
            if (it != allAvgGlobalTrustValues.end()) 
            {
                std::stringstream ss;
                ss << vspType << " VSP" << vspId << " AGT History: [";
                for (size_t j = 0; j < it->second.size(); ++j) 
                {
                    ss << it->second[j];
                    if (j < it->second.size() - 1) 
                    {
                        ss << ", ";
                    }
                }
                ss << "]";
                NS_LOG_INFO(ss.str());
            }
        }
    }

    else if (caseType == DOUBLE_ATTACK) 
    {
        Ptr<RSU> rsuApp = DynamicCast<RSU>(rsuNodes.Get(0)->GetApplication(0));
        auto allAvgGlobalTrustValues = rsuApp->GetAllAvgGlobalTrustValues();
        NS_LOG_INFO("Average Global Trust Values History for Case " << caseType << ":");
        for (uint32_t i = 0; i < positiveVSPs.GetN(); i++) 
        {
            uint32_t vspId = positiveVSPs.Get(i)->GetId();
            auto it = allAvgGlobalTrustValues.find(vspId);
            if (it != allAvgGlobalTrustValues.end()) 
            {
                std::stringstream ss;
                ss << "Positive VSP" << vspId << " AGT History: [";
                for (size_t j = 0; j < it->second.size(); ++j) 
                {
                    ss << it->second[j];
                    if (j < it->second.size() - 1) 
                    {
                        ss << ", ";
                    }
                }
                ss << "]";
                NS_LOG_INFO(ss.str());
            }
        }
        
        for (uint32_t i = 0; i < negativeVSPs.GetN(); i++) 
        {
            uint32_t vspId = negativeVSPs.Get(i)->GetId();
            std::string vspType = (i == 1) ? "OnOff" : "Negative";
            auto it = allAvgGlobalTrustValues.find(vspId);
            if (it != allAvgGlobalTrustValues.end()) 
            {
                std::stringstream ss;
                ss << vspType << " VSP" << vspId << " AGT History: [";
                for (size_t j = 0; j < it->second.size(); ++j) 
                {
                    ss << it->second[j];
                    if (j < it->second.size() - 1) 
                    {
                        ss << ", ";
                    }
                }
                ss << "]";
                NS_LOG_INFO(ss.str());
            }
        }
    }

    else if (caseType >= FCM_TIMING_50_VEHICLES && caseType <= FCM_TIMING_200_VEHICLES) 
    {
        Ptr<RSU> rsuApp = DynamicCast<RSU>(rsuNodes.Get(0)->GetApplication(0));
        double avgFcmTime = rsuApp->GetAverageFcmExecutionTime();
        std::stringstream ss;
        ss << "\n====== FCM TIMING RESULTS: Case " << caseType << " =======\n"
        << "Total vehicles: " << (nHonestVehicles + nBadVehicles) << " (Honest: " << nHonestVehicles 
        << ", Bad: " << nBadVehicles << ")\n"
        << "Number of FCM executions: " << rsuApp->m_fcmExecutionCount << "\n"
        << "Average FCM execution time: " << avgFcmTime << " ms\n"
        << "=====================================";
        NS_LOG_INFO(ss.str());
    }

    Simulator::Destroy();

    // Timing
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    int milliseconds = duration.count() % 1000;
    int seconds = (duration.count() / 1000) % 60;
    int minutes = (duration.count() / (1000 * 60)) % 60;
    int hours = (duration.count() / (1000 * 60 * 60));
    
    std::stringstream ss;
    ss << "\n====== SIMULATION COMPLETE: Case " << caseType << " =======\n"
       << "Vehicles: " << (nHonestVehicles + nBadVehicles) << " (Honest: " << nHonestVehicles 
       << ", Bad: " << nBadVehicles << ")\n"
       << "VSPs: " << (nPositiveVSP + nNegativeVSP) << " (Positive: " << nPositiveVSP 
       << ", Negative: " << nNegativeVSP << ")\n"
       << "Simulation Time: " << simTime << " seconds\n"
       << "Execution Time: " << hours << "h " << minutes << "m " << seconds << "s " 
       << milliseconds << "ms\n"
       << "=====================================";
    NS_LOG_INFO(ss.str());
}

int main(int argc, char *argv[])
{
    CommandLine cmd;
    uint32_t caseType = 1;
    cmd.AddValue("case", "Simulation case (1-25)", caseType);
    cmd.Parse(argc, argv);
    
    if (caseType < 1 || caseType > 25) 
    { 
        NS_LOG_ERROR("Invalid case type: " << caseType << ". Must be between 1 and 21.");
        return 1;
    }
    
    RunSimulationCase(static_cast<SimulationCase>(caseType));
    return 0;
}