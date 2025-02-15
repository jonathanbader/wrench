/**
 * Copyright (c) 2017-2019. The WRENCH Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef SIMULATION_COMPUTESERVICE_H
#define SIMULATION_COMPUTESERVICE_H

#include <map>

#include <iostream>
#include <cfloat>
#include <climits>

#include "wrench/services/Service.h"
#include "wrench/workflow/job/WorkflowJob.h"
#include "wrench/workflow/job/StandardJob.h"
#include "wrench/workflow/job/PilotJob.h"

namespace wrench {

    class Simulation;

    class StorageService;

    /**
     * @brief The compute service base class
     */
    class ComputeService : public Service {

        /***********************/
        /** \cond INTERNAL    **/
        /***********************/

        friend class StandardJobExecutorTest;
        friend class Simulation;

        /***********************/
        /** \endcond          **/
        /***********************/


    public:

        /** @brief A convenient constant to mean "use all cores of a physical host" whenever a number of cores
         *  is needed when instantiating compute services
         */
        static constexpr unsigned long ALL_CORES = ULONG_MAX;

        /** @brief A convenient constant to mean "use all ram of a physical host" whenever a ram capacity
         *  is needed when instantiating compute services
         */
        static constexpr double ALL_RAM = DBL_MAX;

        /***********************/
        /** \cond DEVELOPER   **/
        /***********************/

        virtual ~ComputeService() {}

        void stop() override;

//        void submitJob(WorkflowJob *job, const std::map<std::string, std::string>& = {});

        void terminateJob(std::shared_ptr<WorkflowJob> job);

        bool supportsStandardJobs();

        bool supportsPilotJobs();

        bool hasScratch();

        unsigned long getNumHosts();

        std::vector<std::string> getHosts();

        std::map<std::string, unsigned long> getPerHostNumCores();

        unsigned long getTotalNumCores();

        std::map<std::string, unsigned long> getPerHostNumIdleCores();

        virtual unsigned long getTotalNumIdleCores();

        virtual bool isThereAtLeastOneHostWithIdleResources(unsigned long num_cores, double ram);

        std::map<std::string, double> getMemoryCapacity();

        std::map<std::string, double> getPerHostAvailableMemoryCapacity();

        std::map<std::string, double> getCoreFlopRate();

        double getTTL();

        double getTotalScratchSpaceSize();

        double getFreeScratchSpaceSize();


        /***********************/
        /** \endcond          **/
        /***********************/

        /***********************/
        /** \cond INTERNAL    **/
        /***********************/

        /**
         * @brief Method to submit a standard job to the service
         *
         * @param job: The job being submitted
         * @param service_specific_arguments: the set of service-specific arguments
         */
        virtual void
        submitStandardJob(std::shared_ptr<StandardJob> job, const std::map<std::string, std::string> &service_specific_arguments) = 0;

        /**
         * @brief Method to submit a pilot job to the service
         *
         * @param job: The job being submitted
         * @param service_specific_arguments: the set of service-specific arguments
         */
        virtual void submitPilotJob(std::shared_ptr<PilotJob> job, const std::map<std::string, std::string> &service_specific_arguments) = 0;

        /**
         * @brief Method to terminate a running standard job
         * @param job: the standard job
         */
        virtual void terminateStandardJob(std::shared_ptr<StandardJob> job) = 0;

        /**
         * @brief Method to terminate a running pilot job
         * @param job: the pilot job
         */
        virtual void terminatePilotJob(std::shared_ptr<PilotJob> job) = 0;

        /**
         * @brief Method that returns the computer service's scratch space's storage service
         * @return the scratch space, or nullptr if none
         */
        std::shared_ptr<StorageService> getScratch();


        ComputeService(const std::string &hostname,
                       std::string service_name,
                       std::string mailbox_name_prefix,
                       std::string scratch_space_mount_point);

    protected:

        friend class JobManager;

        void submitJob(std::shared_ptr<WorkflowJob> job, const std::map<std::string, std::string>& = {});

        ComputeService(const std::string &hostname,
                       std::string service_name,
                       std::string mailbox_name_prefix,
                       std::shared_ptr<StorageService> scratch_space);

        /** @brief A scratch storage service associated to the compute service */
        std::shared_ptr<StorageService> scratch_space_storage_service;

        /***********************/
        /** \endcond          **/
        /***********************/

        /***********************/
        /** \cond DEVELOPER   **/
        /***********************/

        std::shared_ptr<StorageService> getScratchSharedPtr();

        /***********************/
        /** \endcond          **/
        /***********************/

    private:


        std::shared_ptr<StorageService> scratch_space_storage_service_shared_ptr;

        std::map<std::string, std::map<std::string, double>> getServiceResourceInformation();

    };


};

#endif //SIMULATION_COMPUTESERVICE_H
