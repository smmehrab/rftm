#ifndef RSU_H
#define RSU_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/socket.h"
#include "ns3/socket-factory.h"
#include "common.h"
#include <vector>
#include <map>
#include <unordered_map>
#include <utility>

using namespace ns3;

struct WeightedRecommendation 
{
    uint32_t vehicleId;
    double trustValue;
    double reliability;
};

struct TrustContext 
{
    uint32_t spId;
    std::vector<uint32_t> interactingVehicles;
    std::map<uint32_t, double> directTrustValues;
    std::vector<WeightedRecommendation> filteredRecommendations;
};

struct PairHash 
{
    std::size_t operator()(const std::pair<uint32_t, uint32_t>& p) const 
    {
        auto h1 = std::hash<uint32_t>{}(p.first);
        auto h2 = std::hash<uint32_t>{}(p.second);
        return h1 ^ (h2 << 1);
    }
};

class RSU : public Application
{
public:
    RSU();
    static TypeId GetTypeId();

    void RegisterVehicle(uint32_t vehicleId, Ipv4Address vehicleAddress, bool isServiceProvider, bool isBadOrAttacker);

    // Sockets
    void HandleAccept(Ptr<Socket> socket, const Address &from);
    void HandleInteractionReport(Ptr<Socket> socket);
    void HandleRegistration(Ptr<Socket> socket);
    void HandleAttackStatusUpdate(Ptr<Socket> socket);
    
    // Trust calculation schedule
    void InitializeTrustCalculations(double simTime);
    void ScheduleNextTrustCalculation(double endTime);

    // Global trust
    void CalculateGlobalTrustValues();
    void CalculateGlobalTrustValueFor(uint32_t spId);
    std::vector<uint32_t> GetInteractingVehiclesFor(uint32_t spId);

    void SetExpectedTrustValue(double value);
    double CalculateMAE(uint32_t spId);

    void SetTrackedVsps(const std::vector<uint32_t>& vspIds);
    std::map<uint32_t, double> GetAverageGlobalTrustValues() const;
    std::map<uint32_t, std::vector<double>> GetAllAvgGlobalTrustValues() const;
    
    // Clustering Algorithm (FCM)
    double GetAverageFcmExecutionTime() const;
    void AddFcmExecutionTime(double time);

    std::vector<double> m_fcmExecutionTimes;
    double m_totalFcmTime;
    int m_fcmExecutionCount;

protected:
    void DoInitialize() override;
    void StartApplication() override;
    void StopApplication() override;

private:
    double GetCachedDirectTrust(uint32_t vehicleId, uint32_t spId);
    double CalculateDirectTrustValue(uint32_t vehicleId, uint32_t spId);
    std::vector<WeightedRecommendation> ExtractRecommendations(const TrustContext& context, uint32_t requesterId);

    std::vector<WeightedRecommendation> FilterRecommendationsFCM(const std::vector<WeightedRecommendation>& recommendations, uint32_t requesterId);
    double CalculateWeightedRecommendedTrust(uint32_t requesterId, const std::vector<WeightedRecommendation>& filteredRecommendations);

    double CalculateAdaptiveWeight(uint32_t requesterId, uint32_t spId);
    double CalculateTimeDifferenceFactor(uint32_t vehicleId, uint32_t spId);
    
    bool HasInteracted(uint32_t vehicleId, uint32_t spId) const;
    void ProcessRequesterTrust(uint32_t requesterId, TrustContext& context);
    void PrecomputeCommonInteractions();
    void PruneInteractionHistory(uint32_t sourceId, uint32_t targetId);
    
    double CalculateSimilarity(uint32_t vehicleId1, uint32_t vehicleId2);
    double CalculateDifferenceLevel(uint32_t recommenderId, uint32_t targetId, const std::vector<WeightedRecommendation>& recommendations);
    double CalculateReliability(uint32_t requesterId, uint32_t recommenderId, const std::vector<WeightedRecommendation>& recommendations);
    bool IsServiceProvider(uint32_t vehicleId);
    std::string GetNodeNickname(uint32_t vehicleId);

    Ptr<UniformRandomVariable> m_uniformVar;
    std::map<uint32_t, Ipv4Address> m_registeredVehicles;

    std::map<uint32_t, std::map<uint32_t, double>> m_trustDatabase;       // [recommender][target] = trust value
    std::map<uint32_t, std::map<uint32_t, double>> m_globalTrustDatabase; // [requester][target] = global trust value

    // Sockets
    Ptr<Socket> SetupListeningSocket(uint16_t port);
    Ptr<Socket> SetupSendingSocket(Ipv4Address destination, uint16_t port);
    Ptr<Socket> m_registrationSocket;           // For vehicle registration
    Ptr<Socket> m_reportSocket;                 // For vehicle reports
    Ptr<Socket> m_attackStatusSocket;           // For attack status updates
    std::map<Address, Ptr<Socket>> m_socketMap; // Map of connected sockets

    // Vehicle tracking
    std::vector<uint32_t> m_serviceProviderIds;
    std::vector<uint32_t> m_badOrAttackerIds;

    // Global trust
    std::map<uint32_t, std::map<uint32_t, int>> m_positiveInteractions; // [sourceId][targetId] = count
    std::map<uint32_t, std::map<uint32_t, int>> m_negativeInteractions; // [sourceId][targetId] = count
    std::map<uint32_t, double> m_averageGlobalTrust;                    // [spId] = average global trust
    std::map<uint32_t, double> m_mae;                    // [spId] = average global trust
    std::map<uint32_t, std::vector<double>> m_allAvgGlobalTrustValues; // [spId] = vector of all global trust values

    double m_expectedTrustValue; // 1.0 for positive VSP, 0.0 for negative VSP
    double m_trustCalcInterval;

    std::map<uint32_t, std::map<uint32_t, std::vector<Interaction>>> m_interactions; // [sourceId][targetId] = vector of interactions

    std::unordered_map<std::pair<uint32_t, uint32_t>, double, PairHash> m_similarityCache;
    std::unordered_map<std::pair<uint32_t, uint32_t>, double, PairHash> m_directTrustCache;
    std::unordered_map<std::pair<uint32_t, uint32_t>, std::vector<uint32_t>, PairHash> m_commonServiceProviders;
    
    // Which VSP to track for global trust calculations (For simulation purpose)
    uint32_t m_trackedVspId;
    bool m_hasTrackedVsp;
    std::vector<uint32_t> m_trackedVspIds;
};

#endif