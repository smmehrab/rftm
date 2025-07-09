#ifndef VEHICLE_H
#define VEHICLE_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/socket.h"
#include "ns3/socket-factory.h"
#include "common.h"
#include <vector>
#include <map>

using namespace ns3;

class RSU;
class Vehicle;

enum ServiceQuality 
{
    NEGATIVE_SERVICE = 0,
    POSITIVE_SERVICE = 1,
};

class Vehicle : public Application
{
public:
    Vehicle();
    static TypeId GetTypeId();

    // Configuration
    void SetNodeType(NodeType type);
    NodeType GetNodeType() const;
    bool IsServiceProvider() const;
    void SetBad(bool bad);
    bool IsBad() const;
    
    void SetAttackType(AttackType type);
    AttackType GetAttackType() const;
    
    void SetServiceQuality(ServiceQuality quality);
    ServiceQuality GetServiceQuality() const;
    
    // Service & Trust
    void RecordInteraction(uint32_t otherId, Feedback feedback, Ipv4Address rsuAddress);
    void ReportInteractionToRsu(uint32_t otherId, Interaction interaction, Ipv4Address rsuAddress);

    void PerformServiceRequest(bool toVSP);
    void RegisterWithRsu();
    void UpdateAttackStatusWithRsu();

    void RequestService(uint32_t serviceProviderId, Ipv4Address targetAddress, Ipv4Address rsuAddress);
    Feedback ProvideService(uint32_t requesterId);
    
    void SetServiceProbability(double probability);
    void SetServiceInterval(double interval);
    void SetVspNonVspInterval(double interval);

    // OnOff
    void ToggleOnOff();
    bool IsInOnOffMode() const;
    void SetIsInOnOffMode(bool isInOnOffMode);
    void SetIsOn(bool isOn);
    double SetOnTime(double onTime);
    double SetOffTime(double offTime);
    void SetStartWithOn(bool startWithOn);

    // Sockets
    void HandleAccept(Ptr<Socket> socket, const Address& from);
    void HandleRead(Ptr<Socket> socket);

protected:
    void DoInitialize() override;
    void StartApplication() override;
    void StopApplication() override;

private:
    Ptr<Socket> SetupListeningSocket(uint16_t port);
    Ptr<Socket> SetupSendingSocket(Ipv4Address destination, uint16_t port);
    
    void SendServiceRequest(Ptr<Socket> socket, uint32_t targetId);
    void SendServiceResponse(Ptr<Socket> socket, uint32_t targetId, Feedback feedback);
    
    Feedback ChangeFeedback(Feedback feedback);
    
    void ToggleServiceQuality();

    NodeType m_nodeType;
    int m_interactionCount;
    bool m_isInOnOffMode;
    bool m_isOn;
    double m_onTime;                    // Time in attack mode
    double m_offTime;                   // Time in normal mode
    bool m_isServiceProvider;
    AttackType m_attackType;
    ServiceQuality m_serviceQuality;
    bool m_isBad;                       // if the vehicle inverts the trust value or not
    double m_serviceInterval;
    double m_vspNonVspInterval;
    double m_endTime;
    bool m_startWithOn;                 // Whether the vehicle starts in ON state (or OFF state)
    double m_serviceProbability;
    
    Ptr<UniformRandomVariable> m_uniformVar;
    std::map<uint32_t, std::vector<Interaction>> m_interactionHistory;
    std::map<uint32_t, std::vector<std::pair<uint32_t, double>>> m_filteredRecommendations;
    Ipv4Address m_rsuAddress;
    std::vector<Ptr<Node>> m_serviceProviderNodes;
    std::vector<Ptr<Node>> m_otherVehicleNodes;
    Ptr<Node> m_rsuNode;
    Ptr<Socket> m_serviceSocket;
    std::map<Address, Ptr<Socket>> m_socketMap;
};

#endif