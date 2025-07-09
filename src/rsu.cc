#include "rsu.h"
#include <chrono>

NS_LOG_COMPONENT_DEFINE("RSU");

RSU::RSU()
{
    m_uniformVar = CreateObject<UniformRandomVariable>();
    m_trustCalcInterval = TRUST_EVAL_INTERVAL;
    m_expectedTrustValue = 0.5;
    m_trackedVspId = 0;
    m_hasTrackedVsp = false;
    m_trackedVspIds.clear();
    m_allAvgGlobalTrustValues.clear();

    m_totalFcmTime = 0.0;
    m_fcmExecutionCount = 0;
    m_fcmExecutionTimes.clear();
}

double RSU::GetAverageFcmExecutionTime() const
{
    if (m_fcmExecutionCount == 0) 
    {
        return 0.0;
    }
    return m_totalFcmTime / m_fcmExecutionCount;
}

void RSU::AddFcmExecutionTime(double time)
{
    m_fcmExecutionTimes.push_back(time);
    m_totalFcmTime += time;
    m_fcmExecutionCount++;
}

std::map<uint32_t, std::vector<double>> RSU::GetAllAvgGlobalTrustValues() const
{
    return m_allAvgGlobalTrustValues;
}

void RSU::SetTrackedVsps(const std::vector<uint32_t>& vspIds)
{
    m_trackedVspIds = vspIds;
    NS_LOG_INFO("RSU" << GetNode()->GetId() << " now tracking " << m_trackedVspIds.size() << " VSPs for global trust calculation");
    for (auto vspId : m_trackedVspIds) 
    {
        NS_LOG_INFO("  - Tracking VSP" << vspId);
    }
}

std::map<uint32_t, double> RSU::GetAverageGlobalTrustValues() const
{
    return m_averageGlobalTrust;
}

TypeId RSU::GetTypeId()
{
    static TypeId tid = TypeId("RSU")
                            .SetParent<Application>()
                            .AddConstructor<RSU>();
    return tid;
}

void RSU::DoInitialize()
{
    Application::DoInitialize();
}

void RSU::StartApplication()
{
    // NS_LOG_INFO("RSU" << GetNode()->GetId() << " Started");

    // Setup sockets
    m_registrationSocket = SetupListeningSocket(REGISTRATION_PORT);
    m_reportSocket = SetupListeningSocket(REPORT_PORT);
    m_attackStatusSocket = SetupListeningSocket(ATTACK_STATUS_PORT);

    // Schedule first global trust calculation
    Simulator::Schedule(Seconds(20.0), &RSU::InitializeTrustCalculations, this, SIMULATION_TIME);
}

void RSU::StopApplication()
{
    if (m_registrationSocket)
    {
        m_registrationSocket->Close();
        m_registrationSocket = 0;
    }

    if (m_reportSocket)
    {
        m_reportSocket->Close();
        m_reportSocket = 0;
    }

    if (m_attackStatusSocket)
    {
        m_attackStatusSocket->Close();
        m_attackStatusSocket = 0;
    }

    for (auto &socketPair : m_socketMap)
    {
        if (socketPair.second)
        {
            socketPair.second->Close();
        }
    }
    m_socketMap.clear();
    // NS_LOG_INFO("RSU" << GetNode()->GetId() << " Stopped");
}

Ptr<Socket> RSU::SetupListeningSocket(uint16_t port)
{
    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    Ptr<Socket> socket = Socket::CreateSocket(GetNode(), tid);

    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), port);
    socket->Bind(local);
    socket->Listen();

    socket->SetAcceptCallback(
        MakeNullCallback<bool, Ptr<Socket>, const Address &>(),
        MakeCallback(&RSU::HandleAccept, this));

    if (port == REGISTRATION_PORT) 
    {
        socket->SetRecvCallback(MakeCallback(&RSU::HandleRegistration, this));
    }
    else if (port == REPORT_PORT) 
    {
        socket->SetRecvCallback(MakeCallback(&RSU::HandleInteractionReport, this));
    }
    else if (port == ATTACK_STATUS_PORT) 
    {
        socket->SetRecvCallback(MakeCallback(&RSU::HandleAttackStatusUpdate, this));
    }
    return socket;
}

Ptr<Socket> RSU::SetupSendingSocket(Ipv4Address destination, uint16_t port)
{
    TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
    Ptr<Socket> socket = Socket::CreateSocket(GetNode(), tid);

    socket->Connect(InetSocketAddress(destination, port));
    return socket;
}

void RSU::HandleAccept(Ptr<Socket> socket, const Address &from)
{
    if (socket == m_registrationSocket) 
    {
        socket->SetRecvCallback(MakeCallback(&RSU::HandleRegistration, this));
        // NS_LOG_DEBUG("RSU" << GetNode()->GetId() << " accepted connection on REGISTRATION port from " << InetSocketAddress::ConvertFrom(from).GetIpv4());
    }
    else if (socket == m_reportSocket) 
    {
        socket->SetRecvCallback(MakeCallback(&RSU::HandleInteractionReport, this));
        // NS_LOG_DEBUG("RSU" << GetNode()->GetId() << " accepted connection on TRUST_REPORT port from " << InetSocketAddress::ConvertFrom(from).GetIpv4());
    }
    else if (socket == m_attackStatusSocket) 
    {
        socket->SetRecvCallback(MakeCallback(&RSU::HandleAttackStatusUpdate, this));
        // NS_LOG_DEBUG("RSU" << GetNode()->GetId() << " accepted connection on ATTACK_STATUS port from " << InetSocketAddress::ConvertFrom(from).GetIpv4());
    }
    else 
    {
        // NS_LOG_ERROR("RSU" << GetNode()->GetId() << " unknown socket in HandleAccept!");
    }
    m_socketMap[from] = socket;
}

void RSU::PruneInteractionHistory(uint32_t sourceId, uint32_t targetId) 
{
    if (m_interactions.find(sourceId) != m_interactions.end() && 
        m_interactions[sourceId].find(targetId) != m_interactions[sourceId].end()) 
    {
        std::vector<Interaction>& interactions = m_interactions[sourceId][targetId];        
        if (interactions.size() > SLIDING_WINDOW) 
        {
            size_t numToRemove = interactions.size() - SLIDING_WINDOW;
            interactions.erase(interactions.begin(), interactions.begin() + numToRemove);
            // NS_LOG_DEBUG("Pruned " << numToRemove << " old interactions from V" << sourceId << " to V" << targetId);
        }
    }
}

void RSU::HandleRegistration(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address from;

    while ((packet = socket->RecvFrom(from)))
    {
        if (packet->GetSize() == 0)
        {
            break;
        }

        Message msg;
        packet->CopyData((uint8_t *)&msg, sizeof(Message));

        if (msg.type != MSG_VEHICLE_REGISTRATION)
        {
            // NS_LOG_ERROR("RSU" << GetNode()->GetId() << " received wrong message type " << msg.type << " on registration handler (expected " << MSG_VEHICLE_REGISTRATION << ")");
            continue;
        }

        // NS_LOG_DEBUG("RSU" << GetNode()->GetId() << " processing registration from V" << msg.sourceId);

        Ipv4Address sourceIp = InetSocketAddress::ConvertFrom(from).GetIpv4();
        bool isServiceProvider = (msg.value >= 1.0);
        bool isBadOrAttacker = (std::fmod(msg.value, 1.0) >= 0.1);

        RegisterVehicle(msg.sourceId, sourceIp, isServiceProvider, isBadOrAttacker);
    }
}

void RSU::HandleAttackStatusUpdate(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address from;

    while ((packet = socket->RecvFrom(from)))
    {
        if (packet->GetSize() == 0)
        {
            break;
        }

        Message msg;
        packet->CopyData((uint8_t *)&msg, sizeof(Message));

        if (msg.type != MSG_UPDATE_ATTACK_STATUS)
        {
            // NS_LOG_ERROR("RSU" << GetNode()->GetId() << " received wrong message type " << msg.type << " on attack status handler (expected " << MSG_UPDATE_ATTACK_STATUS << ")");
            continue;
        }

        uint32_t vehicleId = msg.sourceId;
        AttackType attackType = static_cast<AttackType>(msg.value);
        
        bool isBadOrAttacker = (attackType != NO_ATTACK);

        auto it = std::find(m_badOrAttackerIds.begin(), m_badOrAttackerIds.end(), vehicleId);
        bool isInBadList = (it != m_badOrAttackerIds.end());
        
        if (isBadOrAttacker && !isInBadList) 
        {
            m_badOrAttackerIds.push_back(vehicleId);
            NS_LOG_INFO("✓ V" << vehicleId << " ADDED to bad/attacker list with attack type " << (int)attackType << " (total: " << m_badOrAttackerIds.size() << ")");
        }
        else if (!isBadOrAttacker && isInBadList) 
        {
            m_badOrAttackerIds.erase(it);
            NS_LOG_INFO("✓ V" << vehicleId << " REMOVED from bad/attacker list" << " (total: " << m_badOrAttackerIds.size() << ")");
        }
        else if (isBadOrAttacker && isInBadList) 
        {
            NS_LOG_INFO("✓ V" << vehicleId << " attack type UPDATED to " << (int)attackType << " (already in bad list)");
        }
    }
}

void RSU::HandleInteractionReport(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address from;

    while ((packet = socket->RecvFrom(from)))
    {
        if (packet->GetSize() == 0)
        {
            break;
        }

        Message msg;
        packet->CopyData((uint8_t *)&msg, sizeof(Message));

        if (msg.type != MSG_TRUST_INTERACTION_REPORT)
        {
            // NS_LOG_ERROR("RSU" << GetNode()->GetId() << " received wrong message type " << msg.type << " on trust report handler (expected " << MSG_TRUST_INTERACTION_REPORT << ")");
            continue;
        }

        // NS_LOG_DEBUG("RSU" << GetNode()->GetId() << " processing trust report from V" << msg.sourceId << " about V" << msg.targetId);

        Interaction interaction;
        interaction.timestamp = msg.timestamp;
        interaction.feedback = (msg.value > 0.5) ? POSITIVE : NEGATIVE;

        m_interactions[msg.sourceId][msg.targetId].push_back(interaction);

        if (interaction.feedback == POSITIVE)
            m_positiveInteractions[msg.sourceId][msg.targetId]++;
        else
            m_negativeInteractions[msg.sourceId][msg.targetId]++;

        PruneInteractionHistory(msg.sourceId, msg.targetId);

        auto key = std::make_pair(msg.sourceId, msg.targetId);
        m_directTrustCache.erase(key);

        // NS_LOG_INFO("RSU" << GetNode()->GetId() << " recorded interaction: V" << msg.sourceId << " -> V" << msg.targetId << " (" << (interaction.feedback == POSITIVE ? "POSITIVE" : "NEGATIVE") << ")");
    }
}


void RSU::RegisterVehicle(uint32_t vehicleId, Ipv4Address vehicleAddress, bool isServiceProvider, bool isBadOrAttacker)
{
    m_registeredVehicles[vehicleId] = vehicleAddress;

    if (isBadOrAttacker) 
    {
        m_badOrAttackerIds.push_back(vehicleId);
    }

    if (isServiceProvider)
    {
        m_serviceProviderIds.push_back(vehicleId);
        // NS_LOG_INFO("VSP" << vehicleId << " (" << vehicleAddress << ") registered with RSU" << GetNode()->GetId());

        // If this is the first VSP, mark it as the one to track
        if (!m_hasTrackedVsp) 
        {
            m_trackedVspId = vehicleId;
            m_hasTrackedVsp = true;
            NS_LOG_INFO("VSP" << vehicleId << " set as the tracked VSP for global trust calculations");
        }
    }
    else
    {
        // NS_LOG_INFO("V" << vehicleId << " (" << vehicleAddress << ") registered with RSU" << GetNode()->GetId());
    }
}

void RSU::PrecomputeCommonInteractions() 
{    
    m_commonServiceProviders.clear();
    
    // For each pair of vehicles
    for (const auto& v1 : m_registeredVehicles) 
    {
        for (const auto& v2 : m_registeredVehicles) 
        {
            // Only compute once per pair
            if (v1.first >= v2.first) 
            {
                continue; 
            }
            
            std::vector<uint32_t> common;
            
            // Find common VSPs they've interacted with
            for (auto spId : m_serviceProviderIds) 
            {
                bool v1Interacted = HasInteracted(v1.first, spId);
                bool v2Interacted = HasInteracted(v2.first, spId);
                
                if (v1Interacted && v2Interacted) 
                {
                    common.push_back(spId);
                }
            }
            
            // Store in cache if they have common VSPs
            if (!common.empty()) 
            {
                m_commonServiceProviders[std::make_pair(v1.first, v2.first)] = common;
                m_commonServiceProviders[std::make_pair(v2.first, v1.first)] = common;
            }
        }
    }
    
    // NS_LOG_INFO("RSU" << GetNode()->GetId() << " precomputed common interactions for " << m_commonServiceProviders.size()/2 << " vehicle pairs");
}

bool RSU::HasInteracted(uint32_t vehicleId, uint32_t spId) const 
{
    auto it1 = m_interactions.find(vehicleId);
    if (it1 != m_interactions.end()) 
    {
        auto it2 = it1->second.find(spId);
        if (it2 != it1->second.end() && !it2->second.empty()) 
        {
            return true;
        }
    }
    return false;
}

std::vector<WeightedRecommendation> RSU::ExtractRecommendations(const TrustContext& context, uint32_t requesterId) 
{
    std::vector<WeightedRecommendation> recommendations;
    
    for (const auto& pair : context.directTrustValues) 
    {
        if (pair.first != requesterId) 
        {
            recommendations.push_back({pair.first, pair.second, 0.0});
        }
    }
    
    return recommendations;
}

std::vector<WeightedRecommendation> RSU::FilterRecommendationsFCM(const std::vector<WeightedRecommendation>& recommendations, uint32_t requesterId)
{
    // auto startTime = std::chrono::high_resolution_clock::now();

    size_t numNodes = recommendations.size();
    if (numNodes == 0)
        return std::vector<WeightedRecommendation>();

    const int numClusters = FCM_NUM_CLUSTERS;
    const double fuzziness = FCM_FUZZINESS;
    const double epsilon = FCM_EPSILON;
    const int maxIter = FCM_MAX_ITER;

    // Store recommendations with calculated reliability
    std::vector<WeightedRecommendation> recommendersWithReliability;
    recommendersWithReliability.reserve(numNodes);
    
    std::vector<double> reliabilityValues;
    std::vector<uint32_t> recommenderIds;
    
    // Calculate reliability for each recommender
    for (auto rec : recommendations) 
    {
        recommenderIds.push_back(rec.vehicleId);
        
        // Calculate similarity
        double similarity = CalculateSimilarity(requesterId, rec.vehicleId);
        
        // Calculate difference level
        double differenceLevel = CalculateDifferenceLevel(rec.vehicleId, rec.vehicleId, recommendations);
        
        // Get trust value from requester to recommender
        double tik = 0.5;
        if (m_trustDatabase.find(requesterId) != m_trustDatabase.end() && m_trustDatabase[requesterId].find(rec.vehicleId) != m_trustDatabase[requesterId].end())
        {
            tik = m_trustDatabase[requesterId][rec.vehicleId];
        }
        else if (HasInteracted(requesterId, rec.vehicleId))
        {
            tik = GetCachedDirectTrust(requesterId, rec.vehicleId);
        }
        
        // Calculate reliability
        double reliability = similarity * (1.0 - differenceLevel) * tik;
        reliabilityValues.push_back(reliability);
        
        // Store the recommendation with its reliability
        rec.reliability = reliability;
        recommendersWithReliability.push_back(rec);
        
        // NS_LOG_DEBUG("Reliability of recommender V" << rec.vehicleId << ": " << reliability << " (similarity: " << similarity << ", difference: " << differenceLevel << ", trust: " << tik << ")");
    }

    // Initialize random membership values
    std::vector<std::vector<double>> membership(numNodes, std::vector<double>(numClusters));
    Ptr<UniformRandomVariable> uniformVar = CreateObject<UniformRandomVariable>();
    for (size_t i = 0; i < numNodes; ++i)
    {
        double sum = 0.0;
        for (int j = 0; j < numClusters; ++j)
        {
            membership[i][j] = uniformVar->GetValue(0, 1);
            sum += membership[i][j];
        }
        // Normalize to ensure the sum equals 1
        for (int j = 0; j < numClusters; ++j)
        {
            membership[i][j] /= sum;
        }
    }
    // NS_LOG_DEBUG("FCM initialized with " << numNodes << " nodes using reliability values");

    // FCM iterations, using reliability values
    std::vector<double> clusterCenters(numClusters, 0.0);
    for (int iter = 0; iter < maxIter; ++iter)
    {
        // Calculate cluster centers 
        for (int j = 0; j < numClusters; ++j)
        {
            double numerator = 0.0, denominator = 0.0;
            for (size_t i = 0; i < numNodes; ++i)
            {
                double u_ij = pow(membership[i][j], fuzziness);
                numerator += u_ij * reliabilityValues[i];  // Use reliability here
                denominator += u_ij;
            }
            clusterCenters[j] = (denominator > 0) ? numerator / denominator : 0.0;
        }

        // Update membership matrix
        double maxChange = 0.0;
        for (size_t i = 0; i < numNodes; ++i)
        {
            for (int j = 0; j < numClusters; ++j)
            {
                double newVal = 0.0;
                double dist = fabs(reliabilityValues[i] - clusterCenters[j]);  // Use reliability here

                if (dist < 1e-9)
                {
                    newVal = (j == 0) ? 1.0 : 0.0;
                }
                else
                {
                    double sum = 0.0;
                    for (int k = 0; k < numClusters; ++k)
                    {
                        double distK = fabs(reliabilityValues[i] - clusterCenters[k]);  // Use reliability here
                        if (distK < 1e-9)
                        {
                            sum = 0.0;
                            break;
                        }
                        sum += pow(dist / distK, 2.0 / (fuzziness - 1));
                    }
                    newVal = (sum > 0) ? 1.0 / sum : 1.0;
                }

                maxChange = std::max(maxChange, fabs(newVal - membership[i][j]));
                membership[i][j] = newVal;
            }
        }

        if (maxChange < epsilon)
        {
            // NS_LOG_DEBUG("FCM converged after " << iter + 1 << " iterations");
            break;
        }
    }

    int trustworthyCluster = (clusterCenters[0] > clusterCenters[1]) ? 0 : 1;
    // NS_LOG_DEBUG("Trustworthy cluster identified as cluster " << trustworthyCluster << " with center value " << clusterCenters[trustworthyCluster]);
    
    // Filter nodes, keep only those with high membership in trustworthy cluster
    std::vector<WeightedRecommendation> filteredRecommenders;
    for (size_t i = 0; i < numNodes; ++i)
    {
        if (membership[i][trustworthyCluster] > TRUSTWORTHY_THRESHOLD)
        {
            filteredRecommenders.push_back(recommendersWithReliability[i]);
            // NS_LOG_DEBUG("Recommender V" << recommendersWithReliability[i].vehicleId << " accepted with membership " << membership[i][trustworthyCluster] << " (reliability: " << recommendersWithReliability[i].reliability << ")");
        }
        else
        {
            // NS_LOG_DEBUG("Recommender V" << recommendersWithReliability[i].vehicleId << " filtered out with membership " << membership[i][trustworthyCluster] << " (reliability: " << recommendersWithReliability[i].reliability << ")");
        }
    }

    // auto endTime = std::chrono::high_resolution_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    // double executionTimeMs = duration.count() / 1000.0; // Convert to milliseconds
    // AddFcmExecutionTime(executionTimeMs);
    // // NS_LOG_INFO("FCM execution time: " << executionTimeMs << " ms for " << numNodes << " nodes");

    return filteredRecommenders;
}

std::vector<uint32_t> RSU::GetInteractingVehiclesFor(uint32_t spId)
{
    std::vector<uint32_t> interactingVehicles;
    int totalInteractions = 0;

    for (const auto &entry : m_positiveInteractions)
    {
        auto it = entry.second.find(spId);
        if (it != entry.second.end())
        {
            totalInteractions += it->second;
            if (std::find(interactingVehicles.begin(), interactingVehicles.end(), entry.first) == interactingVehicles.end())
            {
                interactingVehicles.push_back(entry.first);
            }
            // NS_LOG_INFO("Found " << it->second << " positive interactions from vehicle " << entry.first << " to VSP " << spId);
        }
    }

    for (const auto &entry : m_negativeInteractions)
    {
        auto it = entry.second.find(spId);
        if (it != entry.second.end())
        {
            totalInteractions += it->second;
            if (std::find(interactingVehicles.begin(), interactingVehicles.end(), entry.first) == interactingVehicles.end())
            {
                interactingVehicles.push_back(entry.first);
            }
            // NS_LOG_INFO("Found " << it->second << " negative interactions from vehicle " << entry.first << " to VSP " << spId);
        }
    }
    
    // std::string interactingVehiclesList = "";
    // for (size_t i = 0; i < interactingVehicles.size(); i++) 
    // {
    //     interactingVehiclesList += GetNodeNickname(interactingVehicles[i]);
    //     if (i < interactingVehicles.size() - 1) 
    //     {
    //         interactingVehiclesList += ", ";
    //     }
    // }

    // NS_LOG_INFO("Interactions with VSP" << spId << ": Total=" << totalInteractions  << ", Vehicles=" << interactingVehicles.size() << " [" << interactingVehiclesList << "]");
    return interactingVehicles;
}

double RSU::GetCachedDirectTrust(uint32_t vehicleId, uint32_t spId)
{
    auto key = std::make_pair(vehicleId, spId);
    auto it = m_directTrustCache.find(key);
    
    if (it != m_directTrustCache.end()) 
    {
        return it->second;
    }
    
    double directTrust = CalculateDirectTrustValue(vehicleId, spId);
    
    m_directTrustCache[key] = directTrust;
    m_trustDatabase[vehicleId][spId] = directTrust;
    
    return directTrust;
}

double RSU::CalculateDirectTrustValue(uint32_t v1Id, uint32_t v2Id)
{
    Time currentTime = Simulator::Now();
    double pt = 0.0; // Positive trust value
    double nt = 0.0; // Negative trust value

    // Process each interaction with its own timestamp
    if (m_interactions.find(v1Id) != m_interactions.end() && m_interactions[v1Id].find(v2Id) != m_interactions[v1Id].end())
    {
        const std::vector<Interaction> &interactions = m_interactions[v1Id][v2Id];

        size_t startIdx = (interactions.size() > SLIDING_WINDOW) ? (interactions.size() - SLIDING_WINDOW) : 0;

        double pf = 0.0; // Current positive interactions
        double nf = 0.0; // Current negative interactions

        // Process each interaction with time decay and penalty factor
        for (size_t i = startIdx; i < interactions.size(); i++)
        {
            const Interaction &interaction = interactions[i];

            double timeInterval = (currentTime - interaction.timestamp).GetSeconds();
            double decay = exp(-DECAY_FACTOR * timeInterval);

            // Count interactions after this one (ptq/ntq)
            double ptq = 0.0;
            double ntq = 0.0;
            for (size_t j = i + 1; j < interactions.size(); j++)
            {
                if (interactions[j].feedback == POSITIVE)
                    ptq += 1.0;
                else
                    ntq += 1.0;
            }

            // Update positive and negative counts based on the formula
            if (interaction.feedback == POSITIVE)
            {
                pf += 1.0;
                pt = (decay * ptq) + pf;
            }
            else
            {
                nf += 1.0;
                nt = (decay * ntq) + nf;
            }
        }
    }

    double directTrust = (pt + 1.0) / (pt + (nt * PENALTY_FACTOR) + 2.0);
    // NS_LOG_INFO("Direct trust from " << GetNodeNickname(v1Id) << " to " << GetNodeNickname(v2Id) << ": " << directTrust);
    return directTrust;
}

bool RSU::IsServiceProvider(uint32_t vehicleId) 
{
    return std::find(m_serviceProviderIds.begin(), m_serviceProviderIds.end(), vehicleId) != m_serviceProviderIds.end();
}

std::string RSU::GetNodeNickname(uint32_t vehicleId) 
{    
    return (IsServiceProvider(vehicleId) ? "VSP" : "V") + std::to_string(vehicleId);
}

double RSU::CalculateSimilarity(uint32_t vehicleId1, uint32_t vehicleId2)
{
    auto key = std::make_pair(vehicleId1, vehicleId2);
    auto cacheIt = m_similarityCache.find(key);
    if (cacheIt != m_similarityCache.end()) 
    {
        return cacheIt->second;
    }
    
    auto reverseKey = std::make_pair(vehicleId2, vehicleId1);
    cacheIt = m_similarityCache.find(reverseKey);
    if (cacheIt != m_similarityCache.end()) 
    {
        return cacheIt->second;
    }

    std::vector<uint32_t> commonSPs;
    
    auto commonIt = m_commonServiceProviders.find(key);
    if (commonIt != m_commonServiceProviders.end()) 
    {
        commonSPs = commonIt->second;
    }
    else 
    {
        for (auto spId : m_serviceProviderIds)
        {
            bool v1HasInteracted = HasInteracted(vehicleId1, spId);
            bool v2HasInteracted = HasInteracted(vehicleId2, spId);

            if (v1HasInteracted && v2HasInteracted)
            {
                commonSPs.push_back(spId);
            }
        }
    }

    // If no common SPs, return default similarity
    if (commonSPs.empty())
    {
        // NS_LOG_INFO("No common interactions found between vehicles " << vehicleId1 << " and " << vehicleId2);
        return 0.5;
    }

    // Calculate similarity
    double totalDifference = 0.0;
    for (auto spId : commonSPs)
    {
        double dt1 = GetCachedDirectTrust(vehicleId1, spId);
        double dt2 = GetCachedDirectTrust(vehicleId2, spId);
        totalDifference += std::abs(dt1 - dt2);
    }
    double similarity = 1.0 - (totalDifference / commonSPs.size());

    m_similarityCache[key] = similarity;
    m_similarityCache[reverseKey] = similarity;
    // NS_LOG_INFO("Similarity between V" << vehicleId1 << " and V" << vehicleId2 << ": " << similarity << " (based on " << commonSPs.size() << " common SPs)");
    return similarity;
}

double RSU::CalculateDifferenceLevel(uint32_t recommenderId, uint32_t targetId, const std::vector<WeightedRecommendation>& recommendations)
{
    // If no recommendations, return default difference level
    if (recommendations.empty())
    {
        return 0.5;
    }

    // Calculate average (expectation) of all recommendation trust values for the target
    double sumTrustValues = 0.0;
    for (const auto &rec : recommendations)
    {
        sumTrustValues += rec.trustValue;
    }
    double averageTrustValue = sumTrustValues / recommendations.size();

    // Find the trust value from this recommender
    double recommenderTrustValue = 0.0;
    bool found = false;
    for (const auto &rec : recommendations)
    {
        if (rec.vehicleId == recommenderId)
        {
            recommenderTrustValue = rec.trustValue;
            found = true;
            break;
        }
    }

    // If recommender not found in recommendations, use direct trust calculation
    if (!found)
    {
        // NS_LOG_INFO("Recommender V" << recommenderId << " not found in recommendations for target " << targetId << ", using direct trust value instead");
        recommenderTrustValue = GetCachedDirectTrust(recommenderId, targetId);
    }

    // Calculate normalized difference degree
    double sumDifferences = 0.0;
    for (const auto &rec : recommendations)
    {
        sumDifferences += std::abs(rec.trustValue - averageTrustValue);
    }

    double absoluteDifference = std::abs(recommenderTrustValue - averageTrustValue);
    double normalizedDifference = (sumDifferences > 0) ? (absoluteDifference / sumDifferences) : 0.0;    
    // NS_LOG_INFO("Difference level for recommender V" << recommenderId << " regarding target " << targetId << ": " << normalizedDifference << " (recommender value: " << recommenderTrustValue << ", average value: " << averageTrustValue << ", absolute diff: " << absoluteDifference << ", sum of diffs: " << sumDifferences << ")");
    return normalizedDifference;
}

double RSU::CalculateReliability(uint32_t requesterId, uint32_t recommenderId, const std::vector<WeightedRecommendation>& recommendations)
{
    double similarity = CalculateSimilarity(requesterId, recommenderId);
    double differenceLevel = CalculateDifferenceLevel(recommenderId, recommenderId, recommendations);

    double tik = 0.5;
    
    if (m_trustDatabase.find(requesterId) != m_trustDatabase.end() && m_trustDatabase[requesterId].find(recommenderId) != m_trustDatabase[requesterId].end())
    {
        tik = m_trustDatabase[requesterId][recommenderId];
        // NS_LOG_DEBUG("Found existing trust value from V" << requesterId << " to V" << recommenderId << ": " << tik);
    }
    else if (HasInteracted(requesterId, recommenderId))
    {
        tik = GetCachedDirectTrust(requesterId, recommenderId);
        // NS_LOG_DEBUG("Calculated direct trust from V" << requesterId << " to V" << recommenderId << ": " << tik);
    }
    else
    {
        // NS_LOG_DEBUG("No direct interaction between V" << requesterId << " and V" << recommenderId << ", using default trust: " << tik);
    }

    // reliability = similarity * (1 - differenceLevel) * tik
    double reliability = similarity * (1.0 - differenceLevel) * tik;
    //** NS_LOG_INFO("Reliability of recommender V" << recommenderId << " for requester V" << requesterId<< ": " << reliability << " (similarity: " << similarity<< ", difference: " << differenceLevel<< ", trust value (tik): " << tik << ")");
    return reliability;
}

double RSU::CalculateTimeDifferenceFactor(uint32_t vehicleId, uint32_t spId)
{
    Time currentTime = Simulator::Now();
    Time lastInteractionTime = Time(0);
    bool foundInteraction = false;

    // Find the most recent interaction time
    if (m_interactions.find(vehicleId) != m_interactions.end() && m_interactions[vehicleId].find(spId) != m_interactions[vehicleId].end())
    {
        const std::vector<Interaction> &interactions = m_interactions[vehicleId][spId];
        if (!interactions.empty())
        {
            // Find most recent interaction
            for (const auto &interaction : interactions)
            {
                if (interaction.timestamp > lastInteractionTime)
                {
                    lastInteractionTime = interaction.timestamp;
                    foundInteraction = true;
                }
            }
        }
    }

    if (!foundInteraction)
    {
        return 0.0;
    }

    double timeDiffSec = (currentTime - lastInteractionTime).GetSeconds();
    // NS_LOG_INFO("Time difference for V" << vehicleId << " with VSP" << spId<< ": " << timeDiffSec << " seconds");
    return timeDiffSec;
}

double RSU::CalculateAdaptiveWeight(uint32_t requesterId, uint32_t spId)
{
    int numInteractions = 0;
    if (m_positiveInteractions.find(requesterId) != m_positiveInteractions.end() && m_positiveInteractions[requesterId].find(spId) != m_positiveInteractions[requesterId].end())
    {
        numInteractions += m_positiveInteractions[requesterId][spId];
    }

    if (m_negativeInteractions.find(requesterId) != m_negativeInteractions.end() && m_negativeInteractions[requesterId].find(spId) != m_negativeInteractions[requesterId].end())
    {
        numInteractions += m_negativeInteractions[requesterId][spId];
    }

    double delT = CalculateTimeDifferenceFactor(requesterId, spId);

    // weight = (1 - theta^(e^(-delT) * IN))
    double expDelT = exp(-delT);                 // Calculate e^(-delT)
    double exponent = expDelT * numInteractions; // Multiply by IN
    double weight = 1.0 - pow(THETA, exponent);  // Calculate final weight
    // NS_LOG_INFO("Adaptive weight for V" << requesterId << " regarding VSP" << spId << ": " << weight << " (interactions: " << numInteractions << ", time diff: " << delT << " seconds)");
    return weight;
}

double RSU::CalculateWeightedRecommendedTrust(uint32_t requesterId, const std::vector<WeightedRecommendation>& filteredRecommendations)
{
    double recommendedTrust = 0.5;
    
    if (filteredRecommendations.empty()) 
    {
        // NS_LOG_INFO("No filtered recommenders for requester V" << requesterId);
        return recommendedTrust;
    }
    
    double weightedSum = 0.0;
    double totalWeight = 0.0;
    for (const auto& rec : filteredRecommendations) 
    {
        double reliability = rec.reliability;
        weightedSum += rec.trustValue * reliability;
        totalWeight += reliability;
        // NS_LOG_INFO("Recommender V" << rec.vehicleId << " provides trust value " << rec.trustValue << " with reliability " << reliability);
    }
    
    if (totalWeight > 0) 
    {
        recommendedTrust = weightedSum / totalWeight;
    }
    
    // NS_LOG_INFO("Calculated weighted recommended trust: " << recommendedTrust);
    return recommendedTrust;
}

void RSU::ProcessRequesterTrust(uint32_t requesterId, TrustContext& context)
{
    uint32_t spId = context.spId;
    double directTrust = context.directTrustValues[requesterId];
    std::vector<WeightedRecommendation> recommendations = ExtractRecommendations(context, requesterId);
    
    if (recommendations.empty()) 
    {
        m_globalTrustDatabase[requesterId][spId] = directTrust;
        // NS_LOG_INFO("No recommendations for V" << requesterId << " regarding VSP" << spId << ", using direct trust: " << directTrust);
        return;
    }
    
    std::vector<WeightedRecommendation> filteredRecommendations = FilterRecommendationsFCM(recommendations, requesterId);
    double recommendedTrust = CalculateWeightedRecommendedTrust(requesterId, filteredRecommendations);
    double weight = CalculateAdaptiveWeight(requesterId, spId);
    
    // Use only direct trust if we have too few recommenders
    if (filteredRecommendations.size() < 5) 
    {
        weight = 1.0;
        // NS_LOG_INFO("Too few filtered recommenders (" << filteredRecommendations.size() << "), using only direct trust");
    }
    else {
        // Calculate average direct trust of filtered recommenders
        double sumDirectTrust = 0.0;
        int count = 0;
        for (const auto& rec : filteredRecommendations) 
        {
            auto it = context.directTrustValues.find(rec.vehicleId);
            if (it != context.directTrustValues.end()) 
            {
                sumDirectTrust += it->second;
                count++;
            }
        }
        double avgDirectTrust = (count > 0) ? (sumDirectTrust / count) : 0.0;
        
        // If average direct trust is below threshold, only use direct trust
        if (avgDirectTrust < TRUSTWORTHY_THRESHOLD) 
        {
            weight = 1.0;
            // NS_LOG_INFO("Average direct trust of filtered recommenders (" << avgDirectTrust << ") is below threshold, using only direct trust");
        }
    }

    double globalTrust = weight * directTrust + (1.0 - weight) * recommendedTrust;
    m_globalTrustDatabase[requesterId][spId] = globalTrust;
    //** NS_LOG_INFO("Global trust from V" << requesterId << " to VSP" << spId << ": " << globalTrust << " (DT=" << directTrust << ", RT=" << recommendedTrust << ", w=" << weight << ")");
}

void RSU::CalculateGlobalTrustValueFor(uint32_t spId)
{
    TrustContext context;
    context.spId = spId;
    context.interactingVehicles = GetInteractingVehiclesFor(spId);
    
    if (context.interactingVehicles.empty()) 
    {
        return;
    }

    for (auto vehicleId : context.interactingVehicles) 
    {
        context.directTrustValues[vehicleId] = GetCachedDirectTrust(vehicleId, spId);
    }

    for (auto requesterId : context.interactingVehicles) 
    {
        ProcessRequesterTrust(requesterId, context);
    }

    double sumGlobalTrust = 0.0;
    int count = 0;
    // NS_LOG_INFO("Calculating AGT for VSP" << spId);
    for (auto vehicleId : context.interactingVehicles) 
    {
        if (m_globalTrustDatabase.find(vehicleId) != m_globalTrustDatabase.end() && m_globalTrustDatabase[vehicleId].find(spId) != m_globalTrustDatabase[vehicleId].end()) 
        {
            // Check if this is a bad or attacker vehicle
            bool isBadOrAttacker = std::find(m_badOrAttackerIds.begin(), m_badOrAttackerIds.end(), vehicleId) != m_badOrAttackerIds.end();
            // double randomValue = m_uniformVar->GetValue(0.0, 1.0);

            // NS_LOG_INFO("V" << vehicleId << " to VSP" << spId << "," << " GT: " << m_globalTrustDatabase[vehicleId][spId] << ", isBadOrAttacker: " << isBadOrAttacker);

            // If it's a bad/attacker vehicle, there's a 85% chance we'll skip it
            // if (isBadOrAttacker && randomValue < 0.85) 
            // {
            //     NS_LOG_INFO("Skipping bad/attacker vehicle V" << vehicleId << " for VSP" << spId << " (random value: " << randomValue << ")");
            //     continue;
            // }

            // If it's a bad/attacker vehicle, we'll skip it
            if (isBadOrAttacker) 
            {
                continue;
            }

            sumGlobalTrust += m_globalTrustDatabase[vehicleId][spId];
            count++;
        }
    }

    m_averageGlobalTrust[spId] = (count > 0) ? (sumGlobalTrust / count) : 0.5;
    m_mae[spId] = m_expectedTrustValue == 0.0 ? m_averageGlobalTrust[spId] : 1 - m_averageGlobalTrust[spId];
    // m_mae[spId] = CalculateMAE(spId);
    m_allAvgGlobalTrustValues[spId].push_back(m_averageGlobalTrust[spId]);
    NS_LOG_INFO("VSP" << spId << ", AGT: " << m_averageGlobalTrust[spId] << ", MAE: " << m_mae[spId] << " (" << count << " vehicles' interactions, expectedTrust: " << m_expectedTrustValue << ")");
}

void RSU::CalculateGlobalTrustValues()
{
    if (!m_trackedVspIds.empty()) 
    {
        for (auto vspId : m_trackedVspIds) 
        {
            CalculateGlobalTrustValueFor(vspId);
        }
    }
    else if (m_hasTrackedVsp) 
    {
        CalculateGlobalTrustValueFor(m_trackedVspId);
    }
    else {
        NS_LOG_INFO("No tracked VSP available for global trust calculation");
    }
}

void RSU::InitializeTrustCalculations(double simTime)
{
    // NS_LOG_INFO("RSU"<< GetNode()->GetId() << " initializing periodic trust calculations, interval: "<< m_trustCalcInterval << " seconds, sim time: " << simTime << " seconds");
    PrecomputeCommonInteractions();
    CalculateGlobalTrustValues();
    ScheduleNextTrustCalculation(simTime);
}

void RSU::ScheduleNextTrustCalculation(double endTime)
{
    double nextTime = Simulator::Now().GetSeconds() + m_trustCalcInterval;
    if (nextTime >= endTime)
    {
        // NS_LOG_INFO("RSU"<< GetNode()->GetId() << " stopping trust calculations, end time reached");
        return;
    }

    // NS_LOG_INFO("RSU"<< GetNode()->GetId() << " scheduling next trust calculation at time " << nextTime);
    Simulator::Schedule(Seconds(m_trustCalcInterval), &RSU::CalculateGlobalTrustValues, this);
    Simulator::Schedule(Seconds(m_trustCalcInterval), &RSU::ScheduleNextTrustCalculation, this, endTime);
}

void RSU::SetExpectedTrustValue(double value)
{
    m_expectedTrustValue = value;
    // NS_LOG_INFO("RSU" << GetNode()->GetId() << " expected trust value set to " << value);
}

double RSU::CalculateMAE(uint32_t spId)
{
    double sumErrors = 0.0;
    int count = 0;

    for (const auto& vehicleEntry : m_globalTrustDatabase) 
    {
        // uint32_t vehicleId = vehicleEntry.first;
        const auto& spTrustMap = vehicleEntry.second;
        
        auto it = spTrustMap.find(spId);
        if (it != spTrustMap.end()) 
        {
            double trustValue = it->second;
            sumErrors += std::abs(trustValue - m_expectedTrustValue);
            count++;
        }
    }

    if (count == 0) 
    {
        NS_LOG_ERROR("No global trust entries found for VSP" << spId);
        return 0.0;
    }

    return sumErrors / count;
}

NS_OBJECT_ENSURE_REGISTERED(RSU);