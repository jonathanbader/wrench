/**
 * Copyright (c) 2017-2018. The WRENCH Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 */

#include "CloudStandardJobScheduler.h"
#include <wrench/util/UnitParser.h>

#include <fstream>
#include <algorithm>

//#include <climits>
//#include <numeric>

WRENCH_LOG_CATEGORY(cloud_scheduler, "Log category for Cloud Scheduler");

namespace wrench {

    /**
     * @brief Schedule and run a set of ready tasks on available cloud resources
     *
     * @param compute_services: a set of compute services available to run jobs
     * @param tasks: a map of (ready) workflow tasks
     *
     * @throw std::runtime_error
     */
    void CloudStandardJobScheduler::scheduleTasks(const std::set<std::shared_ptr<ComputeService>> &compute_services,
                                                  const std::vector<WorkflowTask *> &tasks) {

        // Check that the right compute_services is passed
        if (compute_services.size() != 1) {
            throw std::runtime_error("This example Cloud Scheduler requires a single compute service");
        }

        auto compute_service = *compute_services.begin();
        auto cloud_service = std::dynamic_pointer_cast<CloudComputeService>(compute_service);

        if (cloud_service == nullptr) {
            throw std::runtime_error("This example Cloud Scheduler can only handle a cloud service");
        }

        // define flop rate
        double flop_rate;

        try {
            flop_rate = UnitParser::parse_compute_speed("1Gf");
        } catch (std::invalid_argument &e) {
            throw;
        }

        // create all vms (if possible) and keep a map of which is running where
        if (true/*compute_service->isThereAtLeastOneHostWithIdleResources(2, 0)*/) { WRENCH_INFO("Setting up VMs");
            std::cerr << "Setting up VMs" << std::endl;
            auto availableCores = cloud_service->getPerHostNumIdleCores();
            auto memCaps = cloud_service->getMemoryCapacity();
            auto coreCaps = cloud_service->getPerHostNumCores();
            for (auto const &host: cloud_service->getExecutionHosts()) {
                // while the host has space for vms
                while (availableCores.at(host) >= 2) {
                    // create new vm
                    auto vm = cloud_service->createVM(2, memCaps.at(host) / (coreCaps.at(host) / 2.0));
                    auto new_vm_cs = cloud_service->startVM(vm, host);
                    this->compute_services_running_on_vms.push_back(new_vm_cs);
                    this->host_vm_lookup[host].push_back(new_vm_cs);
                    // update amount of cores left (could also just manually subtract 2 from host's entry in the map)
                    availableCores = cloud_service->getPerHostNumIdleCores();
                }
            }
        }

        std::cerr << "VMs OK" << std::endl;WRENCH_INFO("There are %ld ready tasks to schedule", tasks.size());

/*
        for (auto task: tasks) {

            WRENCH_INFO("Trying to schedule ready task '%s' on a currently running VM", task->getID().c_str());

            // Try to run the task on one of compute services running on previously created VMs
            std::shared_ptr<BareMetalComputeService> picked_vm_cs = nullptr;
            for (auto const &vm_cs: this->compute_services_running_on_vms) {
                unsigned long num_idle_cores;
                try {
                    num_idle_cores = vm_cs->getTotalNumIdleCores();
                } catch (WorkflowExecutionException &e) {
                    // The service has some problem, forget it
                    throw std::runtime_error("Unable to get the number of idle cores: " + e.getCause()->toString());
                }
                if (task->getMinNumCores() <= num_idle_cores) {
                    picked_vm_cs = vm_cs;
                    break;
                }
            }

            // If no current running compute service on a VM can accommodate the task, try
            // to create a new one
            if (picked_vm_cs == nullptr) {
                WRENCH_INFO(
                        "No currently VM can support task '%s', let's try to create one...", task->getID().c_str());
                unsigned long num_idle_cores;
                try {
                    num_idle_cores = cloud_service->getTotalNumIdleCores();
                } catch (WorkflowExecutionException &e) {
                    // The service has some problem, forget it
                    throw std::runtime_error("Unable to get the number of idle cores: " + e.getCause()->toString());
                }
                if (num_idle_cores >= task->getMinNumCores()) {
                    // Create and start the best VM possible for this task
                    try {
                        WRENCH_INFO("Creating a VM with %ld cores", std::min(task->getMinNumCores(), num_idle_cores));
                        auto vm = cloud_service->createVM(std::min(task->getMinNumCores(), num_idle_cores),
                                                          task->getMemoryRequirement());
                        picked_vm_cs = cloud_service->startVM(vm);
                        this->compute_services_running_on_vms.push_back(picked_vm_cs);
                    } catch (WorkflowExecutionException &e) {
                        throw std::runtime_error("Unable to create/start a VM: " + e.getCause()->toString());
                    }
                } else {
                    WRENCH_INFO(
                            "Not enough idle cores on the CloudComputeService to create a big enough VM for task '%s",
                            task->getID().c_str());
                }
            }

            // If no VM is available to run the task, then nevermind
            if (picked_vm_cs == nullptr) {
                continue;
            }

            // loop over runtimes
            for (auto runtime: runtimes) {
                // skip if namespace match
                std::string ns = runtime["wfName"];
                if (ns.find(task->getWFName()) == std::string::npos) {
                    continue;
                }

                // parse instance name from
                // skip if machine typ mismatch
                if (runtime["instanceType"] != "") {
                    continue;
                }

                // remove underscore and convert to lower
                std::string taskname = runtime["taskName"];
                taskname.erase(remove(taskname.begin(), taskname.end(), '_'), taskname.end());
                std::transform(taskname.begin(), taskname.end(), taskname.begin(),
                               [](unsigned char c) { return std::tolower(c); });

                // remove underscore and convert to lower
                std::string taskNameDax = task->getID();
                taskNameDax.erase(remove(taskNameDax.begin(), taskNameDax.end(), '_'), taskNameDax.end());
                std::transform(taskNameDax.begin(), taskNameDax.end(), taskNameDax.begin(),
                               [](unsigned char c) { return std::tolower(c); });

                // if taskname matches
                if (taskNameDax.find(taskname) != std::string::npos) {
                    // get runtime of task in seconds
                    std::string realtime = runtime["realtime"];
                    double rt = std::stod(realtime) / 1000.0;

                    // convert runtime to flops runtime(ms) * flops (1000GF)
                    double flops = rt * flop_rate;

                    // set flops on task (needs setter, since flops are private)
                    task->setFlops(flops);
                }

            }

            WRENCH_INFO("Submitting task '%s' for execution on a VM", task->getID().c_str());

            // Submitting the task
            std::map<WorkflowFile *, std::shared_ptr<FileLocation>> file_locations;
            for (auto f: task->getInputFiles()) {
                file_locations[f] = (FileLocation::LOCATION(default_storage_service));
            }
            for (auto f: task->getOutputFiles()) {
                file_locations[f] = (FileLocation::LOCATION(default_storage_service));
            }
            auto job = this->getJobManager()->createStandardJob(task, file_locations);
            this->getJobManager()->submitJob(job, picked_vm_cs);
        }
*/
        for (auto task: tasks) {

            WRENCH_INFO("Trying to schedule ready task '%s'", task->getID().c_str());

            std::shared_ptr<BareMetalComputeService> picked_vm_cs = nullptr;
            auto prefs = this->rankLookup.getRankingForTask(task->getName());
            for (auto const &host: prefs) {
                if (picked_vm_cs != nullptr) {
                    break;
                }
                // see if this host has available vms
                for (auto const &vm: this->host_vm_lookup.at(host)) {
                    // if this vm has resources to handle the task
                    if (vm->getTotalNumIdleCores() >= 2) {
                        picked_vm_cs = vm;
                        break;
                    }
                }
            }

            if (picked_vm_cs == nullptr) {
                std::cerr << "Skipped ready task '" << task->getName() << "'" << std::endl;
                continue;
            }

            // loop over runtimes
            for (auto runtime: runtimes) {
                // skip if namespace match
                std::string ns = runtime["wfName"];
                if (ns.find(task->getWFName()) == std::string::npos) {
                    continue;
                }

                // parse instance name from
                // skip if machine typ mismatch
                if (runtime["instanceType"] != picked_vm_cs->getHostname()) {
                    continue;
                }

                // remove underscore and convert to lower
                std::string taskname = runtime["taskName"];
                taskname.erase(remove(taskname.begin(), taskname.end(), '_'), taskname.end());
                std::transform(taskname.begin(), taskname.end(), taskname.begin(),
                               [](unsigned char c) { return std::tolower(c); });

                // remove underscore and convert to lower
                std::string taskNameDax = task->getName();
                taskNameDax.erase(remove(taskNameDax.begin(), taskNameDax.end(), '_'), taskNameDax.end());
                std::transform(taskNameDax.begin(), taskNameDax.end(), taskNameDax.begin(),
                               [](unsigned char c) { return std::tolower(c); });

                // if taskname matches
                if (taskNameDax.find(taskname) != std::string::npos) {
                    // get runtime of task in seconds
                    std::string realtime = runtime["realtime"];
                    double rt = std::stod(realtime) / 1000.0;

                    // convert runtime to flops runtime(ms) * flops (1000GF)
                    double flops = rt * flop_rate;

                    // set flops on task (needs setter, since flops are private)
                    task->setFlops(flops);
                }

            }

            WRENCH_INFO("Submitting task '%s' for execution on a VM", task->getName().c_str());

            // Submitting the task
            std::map<WorkflowFile *, std::shared_ptr<FileLocation>> file_locations;
            for (auto f: task->getInputFiles()) {
                file_locations[f] = (FileLocation::LOCATION(default_storage_service));
            }
            for (auto f: task->getOutputFiles()) {
                file_locations[f] = (FileLocation::LOCATION(default_storage_service));
            }
            auto job = this->getJobManager()->createStandardJob(task, file_locations);
            this->getJobManager()->submitJob(job, picked_vm_cs);
        }

        WRENCH_INFO("Done with scheduling tasks as standard jobs");
    }

}
