/**
 * Copyright (c) 2017. The WRENCH Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 *  @brief WRENCH::Simulation is a top-level class that keeps track of
 *  the simulation state.
 */

#include <compute_services/multicore_task_executor/MulticoreTaskExecutor.h>
#include <xbt/log.h>

#include "Simulation.h"
#include "exception/WRENCHException.h"

namespace wrench {

	/**
	 * @brief Default constructor, which creates a simulation
	 */
	Simulation::Simulation() {
		// Customize the logging format
		xbt_log_control_set("root.fmt:[%d][%h:%t(%i)]%e%m%n");

		// Create the S4U simulation wrapper
		this->s4u_simulation = std::unique_ptr<S4U_Simulation>(new S4U_Simulation());
	}

	/**
	 * @brief initializes the simulation
	 *
	 * @param argc
	 * @param argv
	 */
	void Simulation::init(int *argc, char **argv) {
		this->s4u_simulation->initialize(argc, argv);
	}

	/**
	 * @brief Launches the simulation
	 *
	 */
	void Simulation::launch() {
		this->s4u_simulation->runSimulation();
	}

	/**
	 * @brief instantiate a simulated platform
	 *
	 * @param filename is the path to a SimGrid XML platform description file
	 */
	void Simulation::createPlatform(std::string filename) {
		this->s4u_simulation->setupPlatform(filename);
	}

	/**
	 * @brief Instantiate a sequential task executor on a host
	 *
	 * @param hostname is the name of the host in the physical platform
	 */
	void Simulation::createSequentialTaskExecutor(std::string hostname) {

		// Create the compute service
		std::unique_ptr<ComputeService> executor;
		try {
			executor = std::unique_ptr<SequentialTaskExecutor>(new SequentialTaskExecutor(hostname));
		} catch (WRENCHException e) {
			throw e;
		}

		// Add it to the list of Compute Services
		compute_services.push_back(std::move(executor));
		return;
	}

	/**
	 * @brief Instantiate a multicore task executor on a host
	 *
	 * @param hostname is the name of the host in the physical platform
	 */
	void Simulation::createMulticoreTaskExecutor(std::string hostname) {

		// Create the compute service
		std::unique_ptr<ComputeService> executor;
		try {
			executor = std::unique_ptr<MulticoreTaskExecutor>(new MulticoreTaskExecutor(hostname));
		} catch (WRENCHException e) {
			throw e;
		}

		// Add it to the list of Compute Services
		compute_services.push_back(std::move(executor));
		return;
	}

	/**
	 * @brief Instantiate a simple WMS on a host
	 *
	 * @param w is a pointer to the workflow that the WMS will execute
	 * @param hostname is the name of the host on which to start the WMS
	 */
	void Simulation::createSimpleWMS(Workflow *w, std::string hostname) {

		// Create the WMS
		std::unique_ptr<SequentialRandomWMS> wms;
		try {
			wms = std::unique_ptr<SequentialRandomWMS>(new SequentialRandomWMS(this, w, hostname));
		} catch (WRENCHException e) {
			throw e;
		}

		// Add it to the list of WMSes
		WMSes.push_back(std::move(wms));
		return;
	}

	/**
	 * @brief Run a task in an available host
	 *
	 * @param task is a pointer the workflow task
	 * @param callback_mailbox is the name of a mailbox to which a "task done" callback will be sent
	 * @return 0 on success
	 */
	int Simulation::runTask(WorkflowTask *task, std::string callback_mailbox) {
		for (int i = 0; i < this->compute_services.size(); i++) {
			if (this->compute_services[i]->hasIdleCore()) {
				return this->compute_services[i]->runTask(task, callback_mailbox);
			}
		}
		throw WRENCHException("No compute resources!");
	}

	/**
	 * @brief Shutdown all running compute services on the platform
	 */
	void Simulation::shutdown() {

		for (int i = 0; i < this->compute_services.size(); i++) {
			this->compute_services[i]->stop();
		}
	}

};
