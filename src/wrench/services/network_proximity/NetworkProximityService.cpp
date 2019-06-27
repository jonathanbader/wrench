/**
 * Copyright (c) 2017. The WRENCH Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <wrench/logging/TerminalOutput.h>
#include "wrench/services/network_proximity/NetworkProximityService.h"
#include <wrench/simgrid_S4U_util/S4U_Simulation.h>
#include <wrench/simulation/SimulationMessage.h>
#include <wrench/simulation/Simulation.h>
#include <wrench/simgrid_S4U_util/S4U_Mailbox.h>
#include <wrench/services/ServiceMessage.h>
#include "NetworkProximityMessage.h"

#include <wrench/exceptions/WorkflowExecutionException.h>
#include <random>
#include <fstream>
#include <limits>
#include <iomanip>

#include <boost/algorithm/string.hpp>

WRENCH_LOG_NEW_DEFAULT_CATEGORY(network_proximity_service, "Log category for Network Proximity Service");

namespace wrench {

    constexpr double NetworkProximityService::NOT_AVAILABLE;

    /**
     * @brief Destructor
     */
    NetworkProximityService::~NetworkProximityService() {
        this->default_property_values.clear(); // To avoid memory leaks
        this->network_daemons.clear();
    }

    /**
     * @brief Constructor
     * @param hostname: the name of the host on which to start the service
     * @param hosts_in_network: the hosts participating in network measurements
     * @param property_list: a property list ({} means "use all defaults")
     * @param messagepayload_list: a message payload list ({} means "use all defaults")
     */
    NetworkProximityService::NetworkProximityService(std::string hostname,
                                                     std::vector<std::string> hosts_in_network,
                                                     std::map<std::string, std::string> property_list,
                                                     std::map<std::string, double> messagepayload_list
    ) :
            Service(hostname, "network_proximity", "network_proximity") {

        this->hosts_in_network = std::move(hosts_in_network);

        if (this->hosts_in_network.size() < 2) {
            throw std::invalid_argument("NetworkProximityService requires at least 2 hosts to run");
        }

        this->setProperties(default_property_values, property_list);
        this->setMessagePayloads(default_messagepayload_values, messagepayload_list);

        validateProperties();

        // Seed the master_rng
        this->master_rng.seed((unsigned int) (this->getPropertyValueAsDouble(
                wrench::NetworkProximityServiceProperty::NETWORK_PROXIMITY_PEER_LOOKUP_SEED)));
    }

    /**
     * @brief Look up the current (x,y) coordinates of a given host (only for a Vivaldi network service type)
     * @param requested_host: the host whose coordinates are being requested
     * @return A pair:
     *      - a (x,y) coordinate pair
     *      - a timestamp (the oldest timestamp of measurements usead to compute the coordinate)
     *
     * @throw WorkFlowExecutionException
     * @throw std::runtime_error
     */
    std::pair<std::pair<double, double>, double>
    NetworkProximityService::getHostCoordinate(std::string requested_host) {

        assertServiceIsUp();

        if (boost::iequals(
                this->getPropertyValueAsString(NetworkProximityServiceProperty::NETWORK_PROXIMITY_SERVICE_TYPE),
                "alltoall")) {
            throw std::runtime_error(
                    "NetworkProximityService::getCoordinate() cannot be called with NETWORK_PROXIMITY_SERVICE_TYPE of ALLTOALL");
        }

        WRENCH_INFO("Obtaining current coordinates of network daemon on host %s", requested_host.c_str());

        std::string answer_mailbox = S4U_Mailbox::generateUniqueMailboxName("network_get_coordinate_entry");

        try {
            S4U_Mailbox::putMessage(this->mailbox_name,
                                    new CoordinateLookupRequestMessage(answer_mailbox, requested_host,
                                                                       this->getMessagePayloadValue(
                                                                               NetworkProximityServiceMessagePayload::NETWORK_DB_LOOKUP_REQUEST_MESSAGE_PAYLOAD)));
        } catch (std::shared_ptr<NetworkError> &cause) {
            throw WorkflowExecutionException(cause);
        }

        std::shared_ptr<SimulationMessage> message = nullptr;

        try {
            message = S4U_Mailbox::getMessage(answer_mailbox, this->network_timeout);
        } catch (std::shared_ptr<NetworkError> &cause) {
            throw WorkflowExecutionException(cause);
        }

        if (auto msg = std::dynamic_pointer_cast<CoordinateLookupAnswerMessage>(message)) {
            return std::make_pair(msg->xy_coordinate, msg->timestamp);
        } else {
            throw std::runtime_error(
                    "NetworkProximityService::getCoordinate(): Unexpected [" + message->getName() + "] message");
        }
    }

    /**
     * @brief Look up a proximity value in database
     * @param hosts: the pair of hosts whose proximity is of interest
     * @return A pair:
     *           - The proximity value between the pair of hosts
     *           - The timestamp of the oldest measurement use to compute the proximity value
     *
     * @throw WorkflowExecutionException
     * @throw std::runtime_error
     */
    std::pair<double, double> NetworkProximityService::getHostPairDistance(std::pair<std::string, std::string> hosts) {

        assertServiceIsUp();

        std::string network_service_type = this->getPropertyValueAsString(
                NetworkProximityServiceProperty::NETWORK_PROXIMITY_SERVICE_TYPE);

        if (boost::iequals(network_service_type, "alltoall")) {
            WRENCH_INFO("Obtaining the proximity value between %s and %s", hosts.first.c_str(), hosts.second.c_str());
        } else {
            WRENCH_INFO("Obtaining the approximate distance between %s and %s", hosts.first.c_str(),
                        hosts.second.c_str());
        }

        std::string answer_mailbox = S4U_Mailbox::generateUniqueMailboxName("network_query_entry");

        try {
            S4U_Mailbox::dputMessage(this->mailbox_name,
                                     new NetworkProximityLookupRequestMessage(answer_mailbox, std::move(hosts),
                                                                              this->getMessagePayloadValue(
                                                                                      NetworkProximityServiceMessagePayload::NETWORK_DB_LOOKUP_REQUEST_MESSAGE_PAYLOAD)));
        } catch (std::shared_ptr<NetworkError> &cause) {
            throw WorkflowExecutionException(cause);
        }

        std::shared_ptr<SimulationMessage> message = nullptr;

        try {
            message = S4U_Mailbox::getMessage(answer_mailbox, this->network_timeout);
        } catch (std::shared_ptr<NetworkError> &cause) {
            throw WorkflowExecutionException(cause);
        }

        if (auto msg = std::dynamic_pointer_cast<NetworkProximityLookupAnswerMessage>(message)) {
            return std::make_pair(msg->proximity_value, msg->timestamp);
        } else {
            throw std::runtime_error(
                    "NetworkProximityService::query(): Unexpected [" + message->getName() + "] message");
        }
    }

    /**
     * @brief Internal method to add an entry to the database
     * @param pair: a pair of hosts
     * @param proximity_value: proximity value between the pair of hosts
     */
    void NetworkProximityService::addEntryToDatabase(std::pair<std::string, std::string> pair_hosts,
                                                     double proximity_value) {

        WRENCH_INFO("Received new measurement: %s-%s prox=%lf", pair_hosts.first.c_str(), pair_hosts.second.c_str(),
                    proximity_value);
        if (this->entries.find(pair_hosts) == this->entries.end()) {
            std::pair<std::pair<std::string, std::string>, std::pair<double, double>> value = std::make_pair(pair_hosts,
                                                                                                             std::make_pair(
                                                                                                                     proximity_value,
                                                                                                                     Simulation::getCurrentSimulatedDate()));
            this->entries.insert(value);
        } else {
            this->entries[pair_hosts] = std::make_pair(proximity_value, Simulation::getCurrentSimulatedDate());
        }
    }

    /**
     * @brief Main routine of the daemon
     *
     * @return 0 on success, non 0 otherwise
     */
    int NetworkProximityService::main() {

        TerminalOutput::setThisProcessLoggingColor(TerminalOutput::COLOR_MAGENTA);

        WRENCH_INFO("Network Proximity Service starting on host %s!", S4U_Simulation::getHostName().c_str());

        // Create  and start network daemons
        for (auto h : this->hosts_in_network) {
            std::shared_ptr<NetworkProximityDaemon> np_daemon = std::shared_ptr<NetworkProximityDaemon>(
                    new NetworkProximityDaemon(this->simulation, h, this->mailbox_name,
                                               this->getPropertyValueAsDouble(
                                                       NetworkProximityServiceProperty::NETWORK_PROXIMITY_MESSAGE_SIZE),
                                               this->getPropertyValueAsDouble(
                                                       NetworkProximityServiceProperty::NETWORK_PROXIMITY_MEASUREMENT_PERIOD),
                                               this->getPropertyValueAsDouble(
                                                       NetworkProximityServiceProperty::NETWORK_PROXIMITY_MEASUREMENT_PERIOD_MAX_NOISE),
                                               this->messagepayload_list));
            this->network_daemons.push_back(np_daemon);

            // if this network service type is 'vivaldi', setup the coordinate lookup table
            if (boost::iequals(
                    this->getPropertyValueAsString(NetworkProximityServiceProperty::NETWORK_PROXIMITY_SERVICE_TYPE),
                    "vivaldi")) {
//          this->coordinate_lookup_table.insert(std::pair<std::string, std::complex<double>>(h, (0.0)));
                this->coordinate_lookup_table.insert(
                        std::make_pair(h, std::make_pair((0.0), Simulation::getCurrentSimulatedDate())));

            }
        }

        // Start all network daemons
        try {
            for (auto &network_daemon : this->network_daemons) {
                network_daemon->start(network_daemon, true, true); // Daemonized, AUTO RESTART
            }
        } catch (std::runtime_error &e) {
            throw;
        }

        /** Main loop **/
        while (this->processNextMessage()) {

        }

        WRENCH_INFO("Network Proximity Service on host %s cleanly terminating!", S4U_Simulation::getHostName().c_str());
        return 0;
    }

    /**
     * @brief Method to process the next incoming message
     * @return
     */
    bool NetworkProximityService::processNextMessage() {

        S4U_Simulation::computeZeroFlop();

        // Wait for a message
        std::shared_ptr<SimulationMessage> message = nullptr;

        try {
            message = S4U_Mailbox::getMessage(this->mailbox_name);
        } catch (std::shared_ptr<NetworkError> &cause) {
            return true;
        }

        if (message == nullptr) {
            WRENCH_INFO("Got a NULL message... Likely this means we're all done. Aborting!");
            return false;
        }

        WRENCH_DEBUG("Got a [%s] message", message->getName().c_str());

        if (auto msg = std::dynamic_pointer_cast<ServiceStopDaemonMessage>(message)) {

            // This is Synchronous
            try {
                //Stop the network daemons
                std::vector<std::shared_ptr<NetworkProximityDaemon>>::iterator it;
                for (it = this->network_daemons.begin(); it != this->network_daemons.end(); it++) {
                    if ((*it)->isUp()) {
                        (*it)->stop();
                    }
                }
                this->network_daemons.clear();
                this->hosts_in_network.clear();
                S4U_Mailbox::putMessage(msg->ack_mailbox,
                                        new ServiceDaemonStoppedMessage(this->getMessagePayloadValue(
                                                NetworkProximityServiceMessagePayload::DAEMON_STOPPED_MESSAGE_PAYLOAD)));
            } catch (std::shared_ptr<NetworkError> &cause) {
                return false;
            }

        } else if (auto msg = std::dynamic_pointer_cast<NetworkProximityLookupRequestMessage>(message)) {
            double proximity_value = NetworkProximityService::NOT_AVAILABLE;
            double timestamp = -1;

            std::string network_service_type = this->getPropertyValueAsString("NETWORK_PROXIMITY_SERVICE_TYPE");

            if (boost::iequals(network_service_type, "vivaldi")) {
                auto host1 = this->coordinate_lookup_table.find(msg->hosts.first);
                auto host2 = this->coordinate_lookup_table.find(msg->hosts.second);

                if (host1 != this->coordinate_lookup_table.end() && host2 != this->coordinate_lookup_table.end()) {
                    proximity_value = std::sqrt(norm(host2->second.first - host1->second.first));
                    timestamp = std::min(host1->second.second, host2->second.second);
                }
            } else { // alltoall
                if (this->entries.find(msg->hosts) != this->entries.end()) {
                    proximity_value = std::get<0>(this->entries[msg->hosts]);
                    timestamp = std::get<1>(this->entries[msg->hosts]);
                }
            }

            try {
                S4U_Mailbox::dputMessage(msg->answer_mailbox,
                                         new NetworkProximityLookupAnswerMessage(msg->hosts, proximity_value, timestamp,
                                                                                 this->getMessagePayloadValue(
                                                                                         NetworkProximityServiceMessagePayload::NETWORK_DB_LOOKUP_ANSWER_MESSAGE_PAYLOAD)));
            }
            catch (std::shared_ptr<NetworkError> &cause) {
                return true;
            }
            return true;

        } else if (auto msg = std::dynamic_pointer_cast<NetworkProximityComputeAnswerMessage>(message)) {
//            WRENCH_INFO(
//                    "NetworkProximityService::processNextMessage()::Adding proximity value between %s and %s into the database", msg->hosts.first.c_str(), msg->hosts.second.c_str());
            this->addEntryToDatabase(msg->hosts, msg->proximity_value);

            if (boost::iequals(
                    this->getPropertyValueAsString(NetworkProximityServiceProperty::NETWORK_PROXIMITY_SERVICE_TYPE),
                    "vivaldi")) {
                vivaldiUpdate(msg->proximity_value, msg->hosts.first, msg->hosts.second);
            }

            return true;

        } else if (auto msg = std::dynamic_pointer_cast<NextContactDaemonRequestMessage>(message)) {

            std::shared_ptr<NetworkProximityDaemon> chosen_peer = NetworkProximityService::getCommunicationPeer(
                    msg->daemon);

//            unsigned long randNum = (std::rand()%(this->hosts_in_network.size()));

            try {
                S4U_Mailbox::dputMessage(msg->daemon->mailbox_name,
                                         new NextContactDaemonAnswerMessage(chosen_peer->getHostname(),
                                                                            chosen_peer,
                                                                            chosen_peer->mailbox_name,
                                                                            this->getMessagePayloadValue(
                                                                                    NetworkProximityServiceMessagePayload::NETWORK_DAEMON_CONTACT_ANSWER_PAYLOAD)));
            } catch (std::shared_ptr<NetworkError> &cause) {
                return true;
            }
            return true;
        } else if (auto msg = std::dynamic_pointer_cast<CoordinateLookupRequestMessage>(message)) {
            std::string requested_host = msg->requested_host;
            auto const coordinate_itr = this->coordinate_lookup_table.find(requested_host);
            if (coordinate_itr != this->coordinate_lookup_table.cend()) {
                try {
                    S4U_Mailbox::dputMessage(msg->answer_mailbox,
                                             new CoordinateLookupAnswerMessage(requested_host,
                                                                               std::make_pair(
                                                                                       coordinate_itr->second.first.real(),
                                                                                       coordinate_itr->second.first.imag()),
                                                                               coordinate_itr->second.second,
                                                                               this->getMessagePayloadValue(
                                                                                       NetworkProximityServiceMessagePayload::NETWORK_DAEMON_CONTACT_ANSWER_PAYLOAD)));
                } catch (std::shared_ptr<NetworkError> &cause) {
                    return true;
                }
            }
            return true;
        } else {
            throw std::runtime_error(
                    "NetworkProximityService::processNextMessage(): Unknown message type: " +
                    std::to_string(message->payload));
        }
        return false;
    }

    /**
     * @brief Internal method to choose a communication peer for the requesting network proximity daemon
     * @param sender_daemon: the network daemon requesting a peer to communicate with next
     * @return a shared_ptr to the network daemon that is the selected communication peer
     */
    std::shared_ptr<NetworkProximityDaemon>
    NetworkProximityService::getCommunicationPeer(const std::shared_ptr<NetworkProximityDaemon> sender_daemon) {

//        WRENCH_INFO("Obtaining communication peer for %s", sender_daemon->mailbox_name.c_str());

        // coverage will be (0 < coverage <= 1.0) if this is a 'vivaldi' network service
        // else if it is an 'alltoall' network service, coverage is set at 1.0
        double coverage = this->getPropertyValueAsDouble("NETWORK_DAEMON_COMMUNICATION_COVERAGE");
        unsigned long max_pool_size = this->network_daemons.size() - 1;

        // if the network_service type is 'alltoall', sender_daemon selects from a pool of all other network daemons
        // if the network_service type is 'vivaldi', sender_daemon selects from a subset of the max_pool_size
        unsigned long pool_size = (unsigned long) (std::ceil(coverage * max_pool_size));

        std::hash<std::string> hash_func;
        std::default_random_engine sender_rng;
        // uniform distribution to be used by the sending daemon's rng
        std::uniform_int_distribution<unsigned long> s_udist;

        // uniform distribution to be used by master rng
        static std::uniform_int_distribution<unsigned long> m_udist(0, pool_size - 1);

        std::vector<unsigned long> peer_list;

        // all the network daemons EXCEPT the sender get pushed into this vector
        for (unsigned long index = 0; index < this->network_daemons.size(); ++index) {
            if (this->network_daemons[index]->mailbox_name != sender_daemon->mailbox_name) {
                peer_list.push_back(index);
            }
        }

        // set the seed unique to the sending daemon
        sender_rng.seed((unsigned long) hash_func(sender_daemon->mailbox_name));

        std::shuffle(peer_list.begin(), peer_list.end(), sender_rng);

        unsigned long chosen_peer_index = peer_list.at(m_udist(master_rng));

        return std::shared_ptr<NetworkProximityDaemon>(this->network_daemons.at(chosen_peer_index));
    }

    /**
     * @brief Internal method to compute and update coordinates based on Vivaldi algorithm
     * @param proximity_value: one way elapsed time to send a message from the sender to the receiving peer
     * @param sender_hostname: the host at which the sending network daemon resides
     * @param peer_hostname: the host at which the receiving network daemon resides
     */
    void NetworkProximityService::vivaldiUpdate(double proximity_value, std::string sender_hostname,
                                                std::string peer_hostname) {

        const std::complex<double> sensitivity(0.25, 0.25);

        std::complex<double> sender_coordinates, peer_coordinates;

        auto search = this->coordinate_lookup_table.find(
                sender_hostname);
        if (search != this->coordinate_lookup_table.end()) {
            sender_coordinates = search->second.first;
        }

        search = this->coordinate_lookup_table.find(peer_hostname);
        if (search != this->coordinate_lookup_table.end()) {
            peer_coordinates = search->second.first;
        }

        double estimated_distance = std::sqrt(norm(peer_coordinates - sender_coordinates));
        double error = proximity_value - estimated_distance;

        std::complex<double> error_direction, scaled_direction;

        // if both coordinates are at the origin, we need a random direction vector
        if (estimated_distance == 0.0) {
            // TODO: maybe use time(0) from <ctime> as seed: This is just done once! Or just use master_rng?
            static std::default_random_engine direction_rng(0);
            static std::uniform_real_distribution<double> dir_dist(-0.00000000001, 0.00000000001);

            error_direction.real(dir_dist(direction_rng));
            error_direction.imag(dir_dist(direction_rng));
        } else {
            error_direction = sender_coordinates - peer_coordinates;
        }

        // scaled_direction will get start to approach 0 when error_direction gets small
        scaled_direction = error_direction * error;

        // compute the updated sender coordinates
        auto updated_sender_coordinates = sender_coordinates + (scaled_direction * sensitivity);

        // update the coordinate_lookup_table with the updated sender_coordinates
        search = this->coordinate_lookup_table.find(sender_hostname);
        if (search != this->coordinate_lookup_table.end()) {
            search->second.first = updated_sender_coordinates;
            search->second.second = Simulation::getCurrentSimulatedDate();
        }

        WRENCH_DEBUG("Vivaldi updated coordinates of %s from (%f,%f) to (%f,%f)", sender_hostname.c_str(),
                     sender_coordinates.real(), sender_coordinates.imag(), updated_sender_coordinates.real(),
                     updated_sender_coordinates.imag());

        // WRITING VIVALDI RESULTS FOR DEBUGGING
//        std::ofstream output("/home/ryan/Dropbox/Spring18/GRAD_PROJECT/vivaldi_visual/coordinate_table.txt", std::ofstream::app);
//
//        std::map<std::string, std::complex<double>>::iterator it;
//        output.precision(std::numeric_limits<double>::max_digits10);
//        for (it = this->coordinate_lookup_table.begin(); it != this->coordinate_lookup_table.end(); ++it) {
//            output << std::setw(15) << it->first + " | " << std::fixed << it->second.real() << " | " << std::fixed << it->second.imag() << std::endl;
//        }
//        output << "*" << std::endl;
//        output.close();
        // END WRITING VIVALDI RESULTS

    }

    /**
     * @brief Get the network proximity service type
     * @return a string specifying the network proximity service type
     */
    std::string NetworkProximityService::getNetworkProximityServiceType() {
        return this->getPropertyValueAsString(NetworkProximityServiceProperty::NETWORK_PROXIMITY_SERVICE_TYPE);
    }

    // ADD TEST
    /**
     * @brief Internal method to validate Network Proximity Service Properties
     * @throw std::invalid_argument
     */
    void NetworkProximityService::validateProperties() {
        std::string error_prefix = "NetworkProximityService::NetworkProximityService(): ";

        std::string network_service_type = this->getPropertyValueAsString(
                NetworkProximityServiceProperty::NETWORK_PROXIMITY_SERVICE_TYPE);

        if (!boost::iequals(network_service_type, "alltoall") && !boost::iequals(network_service_type, "vivaldi")) {
            throw std::invalid_argument(
                    error_prefix + "Invalid network proximity service type '" +
                    network_service_type +
                    "'");
        }

        double coverage = this->getPropertyValueAsDouble(
                NetworkProximityServiceProperty::NETWORK_DAEMON_COMMUNICATION_COVERAGE);

        if (boost::iequals(network_service_type, "alltoall")) {
            if (coverage != 1.0) {
                throw std::invalid_argument(error_prefix + "Invalid NETWORK_DAEMON_COMMUNICATION_COVERAGE value "
                                            + this->getPropertyValueAsString(
                        NetworkProximityServiceProperty::NETWORK_DAEMON_COMMUNICATION_COVERAGE) +
                                            " for NETWORK_PROXIMITY_SERVICE_TYPE: " + network_service_type);
            }
        } else if (boost::iequals(network_service_type, "vivaldi")) {
            if (coverage <= 0 || coverage > 1) {
                throw std::invalid_argument(error_prefix + "Invalid NETWORK_DAEMON_COMMUNICATION_COVERAGE value " +
                                            this->getPropertyValueAsString(
                                                    NetworkProximityServiceProperty::NETWORK_DAEMON_COMMUNICATION_COVERAGE) +
                                            " for NETWORK_PROXIMITY_SERVICE_TYPE: " + network_service_type);
            }
        }
//      if (this->getMessagePayloadValueAsDouble(NetworkProximityServiceMessagePayload::STOP_DAEMON_MESSAGE_PAYLOAD) < 0) {
//        throw std::invalid_argument(error_prefix + "Invalid STOP_DAEMON_MESSAGE_PAYLOAD value " +
//                                    this->getMessagePayloadValueAsString(
//                                            NetworkProximityServiceMessagePayload::STOP_DAEMON_MESSAGE_PAYLOAD));
//      }
//
//      if (this->getMessagePayloadValueAsDouble(NetworkProximityServiceMessagePayload::DAEMON_STOPPED_MESSAGE_PAYLOAD) < 0) {
//        throw std::invalid_argument(error_prefix + "Invalid DAEMON_STOPPED_MESSAGE_PAYLOAD value " +
//                                    this->getMessagePayloadValueAsString(
//                                            NetworkProximityServiceMessagePayload::DAEMON_STOPPED_MESSAGE_PAYLOAD));
//      }
//
//      if (this->getMessagePayloadValueAsDouble(NetworkProximityServiceMessagePayload::NETWORK_DB_LOOKUP_REQUEST_MESSAGE_PAYLOAD) < 0) {
//        throw std::invalid_argument(error_prefix + "Invalid NETWORK_DB_LOOKUP_REQUEST_MESSAGE_PAYLOAD value " +
//                                    this->getMessagePayloadValueAsString(
//                                            NetworkProximityServiceMessagePayload::NETWORK_DB_LOOKUP_REQUEST_MESSAGE_PAYLOAD));
//      }
//
//      if (this->getMessagePayloadValueAsDouble(NetworkProximityServiceMessagePayload::NETWORK_DB_LOOKUP_ANSWER_MESSAGE_PAYLOAD) < 0) {
//        throw std::invalid_argument(error_prefix + "Invalid NETWORK_DB_LOOKUP_ANSWER_MESSAGE_PAYLOAD value " +
//                                    this->getMessagePayloadValueAsString(
//                                            NetworkProximityServiceMessagePayload::NETWORK_DB_LOOKUP_ANSWER_MESSAGE_PAYLOAD));
//      }
//
//      if (this->getMessagePayloadValueAsDouble(NetworkProximityServiceMessagePayload::NETWORK_DAEMON_CONTACT_REQUEST_PAYLOAD) <
//          0) {
//        throw std::invalid_argument(error_prefix + "Invalid NETWORK_DAEMON_CONTACT_REQUEST_PAYLOAD value " +
//                                    this->getMessagePayloadValueAsString(
//                                            NetworkProximityServiceMessagePayload::NETWORK_DAEMON_CONTACT_REQUEST_PAYLOAD));
//      }
//
//      if (this->getMessagePayloadValueAsDouble(NetworkProximityServiceMessagePayload::NETWORK_DAEMON_CONTACT_ANSWER_PAYLOAD) <
//          0) {
//        throw std::invalid_argument(error_prefix + "Invalid NETWORK_DAEMON_CONTACT_ANSWER_PAYLOAD value " +
//                                    this->getMessagePayloadValueAsString(
//                                            NetworkProximityServiceMessagePayload::NETWORK_DAEMON_CONTACT_ANSWER_PAYLOAD));
//      }
//
//      if (this->getMessagePayloadValueAsDouble(NetworkProximityServiceMessagePayload::NETWORK_DAEMON_MEASUREMENT_REPORTING_PAYLOAD) <
//          0) {
//        throw std::invalid_argument(error_prefix + "Invalid NETWORK_DAEMON_MEASUREMENT_REPORTING_PAYLOAD value " +
//                                    this->getMessagePayloadValueAsString(
//                                            NetworkProximityServiceMessagePayload::NETWORK_DAEMON_MEASUREMENT_REPORTING_PAYLOAD));
//      }

        if (this->getPropertyValueAsDouble(NetworkProximityServiceProperty::LOOKUP_OVERHEAD) < 0) {
            throw std::invalid_argument(error_prefix + "Invalid LOOKUP_OVERHEAD value " +
                                        this->getPropertyValueAsString(
                                                NetworkProximityServiceProperty::LOOKUP_OVERHEAD));
        }

        if (this->getPropertyValueAsDouble(NetworkProximityServiceProperty::NETWORK_PROXIMITY_MESSAGE_SIZE) < 0) {
            throw std::invalid_argument(error_prefix + "Invalid NETWORK_PROXIMITY_MESSAGE_SIZE value " +
                                        this->getPropertyValueAsString(
                                                NetworkProximityServiceProperty::NETWORK_PROXIMITY_MESSAGE_SIZE));
        }

        if (this->getPropertyValueAsDouble(NetworkProximityServiceProperty::NETWORK_PROXIMITY_MEASUREMENT_PERIOD) <=
            0) {
            throw std::invalid_argument(error_prefix + "Invalid NETWORK_PROXIMITY_MEASUREMENT_PERIOD value " +
                                        this->getPropertyValueAsString(
                                                NetworkProximityServiceProperty::NETWORK_PROXIMITY_MEASUREMENT_PERIOD));
        }

        if (this->getPropertyValueAsDouble(
                NetworkProximityServiceProperty::NETWORK_PROXIMITY_MEASUREMENT_PERIOD_MAX_NOISE) < 0) {
            throw std::invalid_argument(error_prefix + "Invalid NETWORK_PROXIMITY_MEASUREMENT_PERIOD_MAX_NOISE value " +
                                        this->getPropertyValueAsString(
                                                NetworkProximityServiceProperty::NETWORK_PROXIMITY_MEASUREMENT_PERIOD_MAX_NOISE));
        }
    }

    /**
     * @brief Gets the list of hosts monitored by this service (does not involve simulated network communications with the service)
     * @return a list of hostnames
     */
    std::vector<std::string> NetworkProximityService::getHostnameList() {
        return this->hosts_in_network;
    }
}
